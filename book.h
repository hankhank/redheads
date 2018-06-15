
constexpr size_t VAR_TEXT_SIZE = 10;
constexpr size_t NULL_ORDER = 0;
constexpr uint64_t NULL_ID = 0;


#pragma pack(1)

enum class BookBehaviours : uint32_t
{
    AMEND_SAMEPRICE_SAMEID = 1 << 0
};

enum class OrderFlags : uint8_t
{
    IS_BID = 1 << 0,
    IS_ASK = 1 << 1,
    IS_FAK = 1 << 2,
};

struct BookInsertReq
{
    uint16_t   mClientId;
    OrderFlags mFlags;
    int64_t    mPrice;
    int64_t    mVolume;
    char       mVarText[VAR_TEXT_SIZE];
};

struct BulkInsertLevel
{
    int64_t mPrice;
    int64_t mVolume;
};

struct BookBulkInsertReq
{
    uint16_t mClientId;
    char     mVarText[VAR_TEXT_SIZE];
    uint8_t  mBids;
    uint8_t  mAsks;
    BulkInsertLevel mQuotes[];
};

struct BookDeleteReq
{
    uint16_t mClientId;
    uint64_t mOrderId;
};

struct BookBulkDeleteReq
{
    uint16_t   mClientId;
    OrderFlags mFlags;
    char       mVarText[VAR_TEXT_SIZE];
};

struct BookAmendReq
{
    uint16_t mClientId;
    uint64_t mOrderId
    int64_t  mPrice;
    int64_t  mVolume;
    bool     mVolumeDelta;
    char     mVarText[VAR_TEXT_SIZE];
};

struct BookInsertInd
{
    uint16_t   mClientId;
    uint64_t   mOrderId;
    OrderFlags mFlags;
    int64_t    mPrice;
    int64_t    mVolume;
};

struct BookDeleteInd
{
    uint16_t mClientId;
    uint64_t mOrderId;
};

struct BookAmendInd
{
    uint16_t mClientId;
    uint64_t mOrigOrderId
    uint64_t mNewOrderId
    int64_t  mPrice;
    int64_t  mVolume;
    bool     mVolumeDelta;
};

struct BookTradeInd
{
    uint64_t mTradeId;
    uint64_t mAggressorClientId;
    uint64_t mPassiveClientId;
    uint64_t mAggressorOrderId;
    uint64_t mPassiveOrderId;
    int64_t  mPrice;
    int64_t  mVolume;
    bool     mAggressorIsBid;
};

struct BookErrorInd
{
    uint16_t mClientId;
    uint64_t mOrderId;
};

#pragma pop()

struct Order
{
    uint16_t mClientId=NULL_ID;
    uint64_t mOrderId=NULL_ID;
    int64_t  mPrice=0;
    int64_t  mVolume=0;
    size_t   mNext=0;
};

struct OrderExtraInfo
{
    char mVarText[VAR_TEXT_SIZE];
};

struct Level
{
    size_t mLead; // First order
    size_t mEnd   // Last order
};

struct SharedBookMem
{
    std::dense_hash_map<uint64_t, size_t> mOrderLookup;
    std::dense_hash_map<uint16_t, std::vector<uint64_t>> mClientOrderLookup;
    std::vector<Order> mOrderPool;
    std::vector<OrderExtraInfo> mOrderExtraInfoPool;
    std::vector<size_t> mOrderFreeList;
    std::vector<size_t> mDroppedLevels;
};

struct IBookClient
{
    virtual ~IBookClient(){}
    virtual void Handle(const BookInsertInd&& ind) = 0;
    virtual void Handle(const BookDeleteInd&& ind) = 0;
    virtual void Handle(const BookAmendInd&& ind) = 0;
    virtual void Handle(const BookTradeInd&& ind) = 0;
    virtual void ImmediateCleanup() = 0;
};

struct Book
{
    Book(uint16_t bookId, uint64_t initOrderId, SharedBookMem& bookMem, IBookClient& client) 
    : mBookId(bookId)
    , mMem(bookMem)
    , mClient(client)
    , mOrderId(initOrderId)
    , mTradeId(initTradeId)
    {
    }
    
    void Init(size_t initLevelAlloc, size_t initOrderAlloc)
    {
        mBids.reserve(initLevelAlloc);
        mAsks.reserve(initLevelAlloc);
    }

    inline SetOrder(size_t newLoc, uint16_t clientId, uint64_t orderId, 
        int64_t price, int64_t volume, const char& varText[VAR_TEXT_SIZE])
    {
        mMem.mOrderLookup[orderId] = newLoc;
        mMem.mOrderExtraInfoPool.emplace(newLoc, varText);
        mMem.mClientOrderLookup[clientId].push_back(orderId);
        mMem.mOrderPool[newLoc] = {orderId, price, volume, 0};
    }

    inline size_t PopSetOrder(uint64_t orderId, uint16_t clientId, 
        int64_t price, int64_t volume, const char& varText[VAR_TEXT_SIZE])
    {
        if(likely_false(mMem.mOrderFreeList.empty()))
        {
            mClient.ImmediateCleanup();
        }
        size_t newLoc = mMem.mOrderFreeList.back();
        mMem.mOrderFreeList.pop();
        mMem.mOrderLookup[orderId] = newLoc;
        mMem.mOrderExtraInfoPool.emplace(newLoc, varText);
        mMem.mClientOrderLookup[clientId].push_back(orderId);
        mMem.mOrderPool.emplace(newLoc, {orderId, price, volume, 0});
        return newLoc;
    }

    inline uint64_t NextOrderId()
    {
        mOrderId = (++mOrderId & 0x0000FFFFFFFFFFFF);
        return (mBookId << 58) | (mOrderId & 0x0000FFFFFFFFFFFF);
    }

    inline uint64_t NextTradeId()
    {
        mTradeId = (++mTradeId & 0x0000FFFFFFFFFFFF);
        return (mBookId << 58) | (mTradeId & 0x0000FFFFFFFFFFFF);
    }

    template<typename T>
    void HandleInsertSide(const BookInsertReq& req, std::vector<Level>& supporting, 
            std::vector<Level>& opposing, T lessAggressive)
    {
        uint64_t orderId = NextOrderId();

        BookInsertAcceptInd insertInd;
        insertInd.mClientId  =  req.mClientId;
        insertInd.mOrderId   =  orderId;
        insertInd.mFlags     =  req.mFlags;
        insertInd.mPrice     =  req.mPrice;
        insertInd.mVolume    =  req.mVolume;
        mClient.Handle(insertInd);

        BookTradeInd tradeInd;
        tradeInd.mAggressorClientId  =  req.mClientId;
        tradeInd.mAggressorOrderId   =  orderId;
        tradeInd.mAggressorIsBid     =  req.mIsBid;

        int64_t remainingVolume = req.mVolume;
        auto aritr = opposing.rbegin();
        // Try trading it
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
                
                tradeInd.mTradeId          =  NextTradeId();
                tradeInd.mPassiveClientId  =  order.mClientId;
                tradeInd.mPassiveOrderId   =  order.mOrderId;
                tradeInd.mPrice            =  order.mPrice;
                tradeInd.mVolume           =  match;
                mClient.Handle(tradeInd);

                if(order.mVolume <= 0)
                {
                    mClient.Handle(BookDeleteInd{order.mClientId, order.mOrderId});
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

        if(!(req.mFlags & IS_FAK) && (remainingVolume > 0))
        {
            auto britr = supporting.rbegin();
            while((britr != supporting.rend()) && lessAggressive(req.mPrice, mMem.mOrderPool[britr->mLead].mPrice)) ++britr;
            if(mMem.mOrderPool[britr->mLead].mPrice == req.mPrice)
            {
                if(mMem.mOrderPool[britr->mLead].mOrderId == NULL_ID) // empty level
                {
                    SetOrder(britr->mLead, orderId, req.mClientId, req.mPrice, req.mVolume, req.mVarText);
                }
                else
                {
                    size_t newLoc = PopSetOrder(orderId, req.mClientId, req.mPrice, req.mVolume, req.mVarText);
                    mMem.mOrderPool[britr->mEnd].mNext = newLoc;
                    britr->mEnd = newLoc;
                }
            }
            else
            {
                size_t newLoc = PopSetOrder(orderId, req.mClientId, req.mPrice, req.mVolume, req.mVarText);
                supporting.emplace(britr, {newLoc, newLoc});
            }
        }
        else
        {
            mClient.Handle(BookDeleteInd{req.mClientId, orderId});
        }
    }

    void Insert(const BookInsertReq& req)
    {
        if(req.mFlags & IS_BID)
        {
            HandleInsertSide(req, mBids, mAsks, std::less);
        }
        else
        {
            HandleInsertSide(req, mAsks, mBids, std::greater);
        }
    }

    void Delete(BookDeleteReq& req)
    {
        auto litr = mMem.mOrderLookup.find(req.mOrderId);
        if(litr == mMem.mOrderLookup.end())
        {
            // todo unknown error
            return;
        }
        assert(mMem.mOrderPool.size() < *litr);

        auto& order = mMem.mOrderPool[*litr];
        if(order.mClientId != req.mClientId)
        {
            mClient.Handle(BookErrorInd{});
        }

        mClient.Handle(BookDeleteInd{order.mClientId, order.mOrderId});
        order.mVolume = 0;
        order.mOrderId = NULL_ID;
        mMem.mOrderLookup.erase(req.mOrderId);
    }
    
    inline bool MatchVarText(const char& pattern, const char& target)
    {
        return strncmp(VAR_TEXT_SIZE, pattern, target) == 0;
    }

    void BulkDelete(BookBulkDeleteReq& req)
    {
        auto colitr = mMem.mClientOrderLookup.find(req.mClientId);
        if(colitr == mMem.mClientOrderLookup.end())
        {
            // todo unknown client
            return;
        }
        
        auto* orderIdItr = colitr->begin(); 
        while(orderIdItr == colitr->end())
        {
            auto litr = mMem.mOrderLookup.find(*orderIdItr);
            if(litr == mMem.mOrderLookup.end())
            {
                orderIdItr = colitr->erase(orderIdItr);
                continue;
            }

            if(MatchVarText(req.mVarText, mMem.mOrderExtraInfoPool[*litr].mVarText))
            {

                auto& order = mMem.mOrderPool[*litr];
                if(order.mFlags & req.mFlags)
                {
                    mClient.Handle(BookDeleteInd{order.mClientId, order.mOrderId});
                    order.mVolume = 0;
                    order.mOrderId = NULL_ID;
                    mMem.mOrderLookup.erase(req.mOrderId);
                }
            }
            ++orderIdItr;
        }
    }

struct BookAmendReq
{
    uint16_t mClientId;
    uint64_t mOrderId
    int64_t  mPrice;
    int64_t  mVolume;
    bool     mVolumeDelta;
    char     mVarText[VAR_TEXT_SIZE];
};

    void Amend(BookAmendReq& req)
    {
        //AMEND_SAMEPRICE_SAMEID
        
        auto litr = mMem.mOrderLookup.find(req.mOrderId);
        if(litr == mMem.mOrderLookup.end())
        {
            // todo unknown error
            return;
        }
        assert(mMem.mOrderPool.size() < *litr);

        auto& order = mMem.mOrderPool[*litr];
        if(order.mClientId != req.mClientId)
        {
            mClient.Handle(BookErrorInd{});
        }

        if(mVolume != 0)
        {
            int64_t newVolume = req.mVolumeDelta ? order.mVolume + req.mVolume : req.mVolume;
        }

        if(mPrice != 0)
        {
            order.mVolume = 0;
            order.mOrderId = NULL_ID;
            mMem.mOrderLookup.erase(req.mOrderId);
        }


    }

    std::vector<Level> mBids; // offset to start and end of level
    std::vector<Level> mAsks;
};

