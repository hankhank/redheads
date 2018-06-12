
#include <iterator>

constexpr size_t NULL_ORDER = 0;
constexpr uint64_t NULL_ID = 0;

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

struct BookInsertReq
{
    Opcode   mCode;
    uint8_t  mAPID;
    uint16_t mTransId;
    uint16_t mBookId;
    uint16_t mClientId;
    bool     mIsBid;
    bool     mIsFak;
    int64_t  mPrice;
    int64_t  mVolume;
};

struct BookDeleteReq
{
    Opcode   mCode;
    uint8_t  mAPID;
    uint16_t mTransId;
    uint16_t mBookId;
    uint16_t mClientId;
    uint64_t mOrderId;
};

struct BookAmendReq
{
    Opcode   mCode;
    uint8_t  mAPID;
    uint16_t mTransId;
    uint16_t mBookId;
    uint16_t mClientId;
    uint64_t mOrderId
    int64_t  mPrice;
    int64_t  mVolume;
};

struct BookAmendDeltaReq
{
    Opcode   mCode;
    uint8_t  mAPID;
    uint16_t mTransId;
    uint16_t mBookId;
    uint16_t mClientId;
    uint64_t mOrderId;
    int64_t  mPrice;
    int64_t  mVolume;
};

struct BookAcceptRsp
{
    uint16_t mTransId;
    uint64_t mTimestamp;
};

struct BookInd
{
    uint16_t mLastTransId;
    uint64_t mTimestamp;
};

struct TradeInd
{
    uint8_t  mAPID;
    uint16_t mTransId;
    uint16_t mBookId;
    uint64_t mAggressorClientId;
    uint64_t mPassiveClientId;
    uint64_t mAggressorOrderId;
    uint64_t mPassiveOrderId;
    int64_t  mPrice;
    int64_t  mVolume;
    bool     mAggressorIsBid;
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
    uint64_t mOrderId=NULL_ID;
    int64_t  mPrice=0;
    int64_t  mVolume=0;
    size_t   mNext=0;
};

struct Level
{
    size_t mLead;
    size_t mEnd
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
        mMem.mOrderPool.emplace(newLoc, {orderId, price, volume, 0});
        return newLoc;
    }

    template<typename T>
    void HandleInsertSide(const BookReq& req, std::vector<Level>& supporting, 
            std::vector<Level>& opposing, T lessAggressive)
    {
        uint64_t orderId = ++mNextOrderId;

        // Try trading it
        int64_t remainingVolume = req.mVolume;
        auto aritr = opposing.rbegin();
        while
        (
            (aritr != opposing.rend()) && 
            (remainingVolume > 0) &&
            (
                 lessAggressive(mMem.mOrderPool[aritr->mLead].mPrice, req.mPrice) ||
                (mMem.mOrderPool[aritr->mLead].mPrice == req.mPrice)
            )
        )
        {
            do
            {
                auto& order = mMem.mOrderPool[aritr->mLead];
                int64_t match = std::min(order.mVolume, remainingVolume);
                remainingVolume -= match;
                order.mVolume -= match;
                // TODO send match
                if(order.mVolume <= 0)
                {
                    mMem.mOrderFreeList.push_back(aritr->mLead);
                    aritr->mLead = order.mNext;
                    order = Order();
                }
            }
            while(aritr->mLead && remainingVolume > 0);

            if(aritr->mLead == NULL_ID)
            {
                opposing.pop_back();
            }
            ++aritr;
        }

        if(!req.mIsFak && (remainingVolume > 0))
        {
            auto britr = supporting.rbegin();
            while((britr != supporting.rend()) && lessAggressive(req.mPrice, mMem.mOrderPool[britr->mLead].mPrice)) ++britr;
            if(mMem.mOrderPool[britr->mLead].mPrice == req.mPrice)
            {
                if(mMem.mOrderPool[britr->mLead].mOrderId == NULL_ID) // empty level
                {
                    mMem.mOrderPool[britr->mLead] = {orderId, req.mPrice, req.mVolume, 0};
                    mMem.mOrderLookup[orderId] = britr->mLead;
                }
                else
                {
                    size_t newLoc = PopSetOrder(orderId, req.mPrice, req.mVolume);
                    mMem.mOrderPool[britr->mEnd].mNext = newLoc;
                    britr->mEnd = newLoc;
                }
            }
            else
            {
                size_t newLoc = PopSetOrder(orderId, req.mPrice, req.mVolume);
                supporting.emplace(britr, {newLoc, newLoc});
            }
            // broadcast resting order
        }
        else
        {
            // todo broadcast fak final state
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
        order.mOrderId = NULL_ID;
        // do we need to keep the level this order was on
        // need a way to clean up old orders better
        // check this user can delete it
    }

    void Amend(BookReq& req)
    {
        Delete(req);
        Insert(req);
        // todo handle as delete+insert
    }

    std::vector<Level> mBids; // offset to start and end of level
    std::vector<Level> mAsks;
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
