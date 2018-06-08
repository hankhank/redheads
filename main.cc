
#pragma pack(1)

enum class Opcode : uint8_t
{
    OP_INSERT,
    OP_DELETE,
    OP_AMEND,

    OP_BULK_INSERT,
    OP_BULK_DELETE,

    OP_BOOK_CREATE,
    OP_BOOK_CLEAR,
    OP_BOOK_DELETE
};

struct BookReq
{
    uint16_t mBookId;
    Opcode   mCode;
    uint16_t mTransId;
    int64_t  mPrice;
    int64_t  mVolume;
    bool     mIsBid;
    bool     mIsFak;
};

struct ExtBookReq
{
    uint16_t mBookId;
    Opcode   mCode;
    uint16_t mTransId;
    uint64_t mOrderId;
    char     tag[20];
};

struct BookRsp
{
    Opcode   mCode;
    uint16_t mBookId;
    uint16_t mTransId;
    uint64_t mOrderId;
    int64_t  mPrice;
    int64_t  mVolume;
    bool     mIsBid;
    uint64_t mTimestamp;
};

struct TradeInd
{
    uint16_t mBookId;
    uint16_t mTransId;
    uint64_t mOrderId;
    int64_t  mPrice;
    int64_t  mVolume;
    bool     mAggressorIsBid;
    uint64_t mTimestamp;
};

struct BookInd
{
    uint16_t mLastTransId;
    uint64_t mTimestamp;
};

struct ManagementReq
{
    Opcode mCode;
    uint16_t mBookId;
};

#pragma pop()

struct Order
{
    uint64_t mOrderId=0;
    int64_t  mPrice=0;
    int64_t  mVolume=0;
    size_t   mNext=0;
};

struct PartMem
{
    std::dense_hash_map<uint64_t, size_t> mOrderLookup;
    std::vector<Order> mOrderPool;
    std::vector<size_t> mOrderFreeList;
    std::vector<size_t> mDroppedLevels;
};

class Book
{
public:
    Book(PartMem& partMem)
    : mMem(partMem)
    {}
    
    void Init(size_t initLevelAlloc, size_t initOrderAlloc)
    {
        mBids.reserve(initLevelAlloc);
        mAsks.reserve(initLevelAlloc);
    }

    inline size_t PopSetOrder(uint64_t orderId, int64_t price, int64_t volume)
    {
        if(likely_false(mMem.mOrderFreeList.empty()))
        {
            PostProcess();
        }
        size_t newLoc = mMem.mOrderFreeList.back();
        mMem.mOrderFreeList.pop();
        mMem.mOrderLookup[orderId] = newLoc;
        mMem.mOrderPool[newLoc] = {orderId, price, volume, 0};
        return newLoc;
    }

    inline void JoinLevel(size_t lead, size_t end)
    {
        size_t lastLoc = lead;
        size_t nextLoc = mMem.mOrderPool[lead].mNext;
        while(nextLoc) 
        {
            lastLoc = nextLoc;
            nextLoc = mMem.mOrderPool[nextLoc].mNext;
        }
        mMem.mOrderPool[lastLoc].mNext = end;
    }

    inline bool TradeLevel(uint64_t orderId, size_t lead, int64_t& remainingVolume)
    {
        while(lead && remainingVolume > 0)
        {
            auto& order = mMem.mOrderPool[lead];
            int64_t match = std::min(order.mVolume, remainingVolume);
            order.mVolume -= match;
            remainingVolume -= match;
            // TODO send match
            lead = order.mNext;
        }
        return ((lead == 0) && (remainingVolume > 0));
    }
    
    template<typename T>
    void HandleInsertSide(const BookReq& req, std::vector<size_t>& supporting, std::vector<size_t>& opposing, T lessAggressive)
    {
        uint64_t orderId = ++mNextOrderId;
        if(!opposing.empty() && 
            ((req.mPrice == mMem.mOrderPool[opposing.back()].mPrice) ||
            lessAggressive(mMem.mOrderPool[opposing.back()].mPrice, req.mPrice)))
        {
            int64_t remainingVolume = req.mVolume;
            auto aritr = opposing.rbegin();
            while((aritr != opposing.rend()) && 
                    (remainingVolume > 0) &&
                    (lessAggressive(mMem.mOrderPool[*aritr].mPrice, req.mPrice) ||
                    (mMem.mOrderPool[*aritr].mPrice == req.mPrice)))
            {
                if(TradeLevel(orderId, *aritr, remainingVolume))
                {
                    mDroppedLevels.push_back(*aritr);
                    opposing.pop_back();
                }
            }
            if(!req.mIsFak && remainingVolume > 0)
            {
                size_t newLoc = PopSetOrder(orderId, req.mPrice, remainingVolume);
                supporting.push_back(newLoc);
                // publish resting order
            }
            else
            {
                // todo broadcast fak
            }
        }
        else if(!req.mIsFak)
        {
            auto britr = supporting.rbegin();
            while((britr != supporting.rend()) && lessAggressive(req.mPrice, mMem.mOrderPool[*britr].mPrice)) ++britr;
            if(mMem.mOrderPool[*britr].mPrice == req.mPrice)
            {
                if(mMem.mOrderPool[*britr].mOrderId == 0)
                {
                    mMem.mOrderPool[*britr] = {orderId, req.mPrice, req.mVolume, 0};
                    mMem.mOrderLookup[orderId] = *britr;
                }
                else
                {
                    size_t newLoc = PopSetOrder(orderId, req.mPrice, req.mVolume);
                    JoinLevel(*britr, newLoc);
                }
            }
            else
            {
                size_t newLoc = PopSetOrder(orderId, req.mPrice, req.mVolume);
                supporting.insert(britr, newLoc);
            }
            // broadcast resting order
        }
        else // fak miss
        {
            // todo broadcast fak miss
        }
    }

    void Insert(const BookReq& req)
    {
        if(req.mIsBid)
        {
            HandleInsertSide(req, mBids, mAsks, std::less);
        }
        else
        {
            HandleInsertSide(req, mAsks, mBids, std::greater);
        }
    }

    void Delete(BookReq& req)
    {
        auto litr = mMem.mOrderLookup.find(req.mOrderId);
        if(litr == mMem.mOrderLookup.end())
        {
            return;
        }
        
        auto& order = mMem.mOrderPool[*litr];
        order.mVolume = 0;
        order.mOrderId = 0;
        // do we need to keep the level this order was on
        // need a way to clean up old orders better
    }

    std::vector<size_t> mBids;
    std::vector<size_t> mAsks;
};

class Partition
{
public:
    Partition() {}
    
    void Init(size_t initLevelAlloc, size_t initOrderAlloc)
    {
        assert(mMem.mOrderPool.empty() && "Can only init allocate once");
        mOrderLookup.resize(initOrderAlloc);
        mOrderFreeList.reserve(initOrderAlloc);
        for(size_t i = 0; i < initOrderAlloc; ++i) mOrderFreeList.push_back(i);
        mDroppedLevels.reserve(64);
    }

    void PostProcess()
    {
        for(auto leadingLoc : mDroppedLevels)
        {
            size_t nextLoc = leadingLoc;
            do
            {
                mOrderFreeList.push_back(nextLoc);
                size_t nextNextLoc = mMem.mOrderPool[nextLoc].mNext;
                mMem.mOrderPool[nextLoc] = Order();
                nextOrder = nextNextLoc;
            }
            while(nextLoc);
        }

        if(mOrderFreeList.empty()())
        {
            size_t origSize = mMem.mOrderPool.size();
            mMem.mOrderPool.resize(2*mMem.mOrderPool.size());
            for(size_t i = origSize; i < 2*origSize; ++i) mOrderFreeList.push_back(i);
        }
    }

    PartMem mMem;
};

int main()
{
    while()
    {
        case OP_INSERT:
        {
        }
        break;

        case OP_DELETE,
        {
        }
        break;

        case OP_AMEND,
        {
        }
        break;

        case OP_BULK_INSERT,
        {
        }
        break;

        case OP_BULK_DELETE,
        {
        }
        break;

        case OP_BOOK_CREATE,
        {
        }
        break;

        case OP_BOOK_CLEAR,
        {
        }
        break;

        case OP_BOOK_DELETE
        {
        }
        break;
    }
}
