
namespace redheads
{

constexpr size_t   VAR_TEXT_SIZE = 10;
constexpr size_t   NULL_ORDER    = 0;
constexpr uint64_t NULL_ID       = 0;

#pragma pack(1)

enum class BookBehaviours : uint32_t
{
    AMEND_SAMEQP_SAMEID = 1 << 0
};

enum class OrderFlags : uint8_t
{
    IS_BID = 1 << 0,
    IS_ASK = 1 << 1,
    IS_FAK = 1 << 2,
};

enum class ErrorCode : uint8_t
{
    OK                   = 0;
    UNKNOWN_ORDER        = 1 << 0,
    NOT_CLIENT_ORDER     = 1 << 1,
    CLIENT_HAS_NO_ORDERS = 1 << 2,
};

struct BookClearReq
{
};

struct BookInsertReq
{
    uint16_t   mClientId;
    OrderFlags mFlags;
    int64_t    mPrice;
    int64_t    mVolume;
    char       mVarText[VAR_TEXT_SIZE];
};

struct QuoteLevel
{
    int64_t mPrice;
    int64_t mVolume;
};

struct BookQuoteReq
{
    uint16_t   mClientId;
    char       mVarText[VAR_TEXT_SIZE];
    uint8_t    mBids;
    uint8_t    mAsks;
    QuoteLevel mQuotes[];
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

struct BookClearInd
{
    uint16_t mBookId;
};

struct BookInsertInd
{
    uint16_t   mBookId;
    uint16_t   mClientId;
    uint64_t   mOrderId;
    OrderFlags mFlags;
    int64_t    mPrice;
    int64_t    mVolume;
};

struct BookDeleteInd
{
    uint16_t mBookId;
    uint16_t mClientId;
    uint64_t mOrderId;
};

struct BookAmendInd
{
    uint16_t mBookId;
    uint16_t mClientId;
    uint64_t mOrigOrderId
    uint64_t mNewOrderId
    int64_t  mPrice;
    int64_t  mVolume;
    bool     mVolumeDelta;
};

struct BookTradeInd
{
    uint16_t mBookId;
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
    uint16_t  mBookId;
    uint16_t  mClientId;
    uint64_t  mOrderId;
    ErrorCode mCode;
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
    virtual void Handle(const BookClearInd&& ind) = 0;
    virtual void Handle(const BookInsertInd&& ind) = 0;
    virtual void Handle(const BookDeleteInd&& ind) = 0;
    virtual void Handle(const BookAmendInd&& ind) = 0;
    virtual void Handle(const BookTradeInd&& ind) = 0;
    virtual void ImmediateCleanup() = 0;
};

struct Book
{
    Book(BookBehaviours behaviours, uint16_t bookId, uint64_t initOrderId, SharedBookMem& bookMem, IBookClient& client) 
    : mBehaviours(behaviours)
    , mBookId(bookId)
    , mMem(bookMem)
    , mClient(client)
    , mOrderId(initOrderId)
    , mTradeId(initTradeId)
    {
        mBids.reserve(100);
        mAsks.reserve(100);
    }
    
    inline SetOrder(size_t newLoc, uint16_t clientId, uint64_t orderId, 
        int64_t price, int64_t volume, const char& varText[VAR_TEXT_SIZE])
    {
        mMem.mOrderLookup[orderId] = newLoc;
        mMem.mOrderExtraInfoPool.emplace(newLoc, {varText});
        mMem.mClientOrderLookup[clientId].push_back(orderId);
        mMem.mOrderPool.emplace(newLoc, {clientId, orderId, price, volume, NULL_ORDER});
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
        mMem.mOrderExtraInfoPool.emplace(newLoc, {varText});
        mMem.mClientOrderLookup[clientId].push_back(orderId);
        mMem.mOrderPool.emplace(newLoc, {clientId, orderId, price, volume, NULL_ORDER});
        return newLoc;
    }

    inline uint64_t NextOrderId()
    {
        mOrderId = (++mOrderId & 0x0000FFFFFFFFFFFF);
        return (mBookId << 48) | (mOrderId & 0x0000FFFFFFFFFFFF);
    }

    inline uint64_t NextTradeId()
    {
        mTradeId = (++mTradeId & 0x0000FFFFFFFFFFFF);
        return (mBookId << 48) | (mTradeId & 0x0000FFFFFFFFFFFF);
    }

    void ProcessDelete(Order& order)
    {
        mClient.Handle(BookDeleteInd{mBookId, order.mClientId, order.mOrderId});
        mMem.mOrderLookup.erase(order.mOrderId);
        order.mVolume = 0;
        order.mOrderId = NULL_ID;
        // if it was top level we might need to remove it
    }

    template<typename T>
    size_t ProcessInsertSide(uint64_t orderId, uint16_t clientId, int64_t price, 
        int64_t volume, OrderFlags flags, const char& varText[VAR_TEXT_SIZE],
        std::vector<Level>& supporting, std::vector<Level>& opposing, T lessAggressive)
    {
        size_t restingLoc = NULL_ORDER;
        BookTradeInd tradeInd;
        tradeInd.mBookId             =  mBookId;
        tradeInd.mAggressorClientId  =  clientId;
        tradeInd.mAggressorOrderId   =  orderId;
        tradeInd.mAggressorIsBid     =  flags & IS_BID;

        int64_t remainingVolume = volume;
        auto opitr = opposing.rbegin();

        // Try trading it
        while
        (
            (opitr != opposing.rend()) && 
            (remainingVolume > 0) &&
            (
                 lessAggressive(mMem.mOrderPool[opitr->mLead].mPrice, price) ||
                (mMem.mOrderPool[opitr->mLead].mPrice == price)
            )
        )
        {
            do
            {
                auto& order = mMem.mOrderPool[opitr->mLead];
                int64_t match = std::min(order.mVolume, remainingVolume);
                if(match > 0)
                {
                    remainingVolume -= match;
                    order.mVolume -= match;
                    
                    tradeInd.mTradeId          =  NextTradeId();
                    tradeInd.mPassiveClientId  =  order.mClientId;
                    tradeInd.mPassiveOrderId   =  order.mOrderId;
                    tradeInd.mPrice            =  order.mPrice;
                    tradeInd.mVolume           =  match;
                    mClient.Handle(tradeInd);
                }

                if(order.mVolume <= 0)
                {
                    mMem.mOrderFreeList.push_back(opitr->mLead);
                    opitr->mLead = order.mNext;
                    if(order.mOrderId != NULL_ID) ProcessDelete(order);
                }
            }
            while(opitr->mLead && remainingVolume > 0);

            if(opitr->mLead == NULL_ORDER)
            {
                mMem.mDroppedLevels.push_back(opitr->mLead);
                opposing.pop_back();
            }
            ++opitr;
        }

        if(!(flags & IS_FAK) && (remainingVolume > 0))
        {
            auto britr = supporting.rbegin();
            // TODO instead of a linear search here I think we could linear search < 10 tops levels then fall
            // back to binary search
            while((britr != supporting.rend()) && lessAggressive(price, mMem.mOrderPool[britr->mLead].mPrice)) ++britr;
            if(mMem.mOrderPool[britr->mLead].mPrice == price)
            {
                if(mMem.mOrderPool[britr->mLead].mOrderId == NULL_ID) // empty level
                {
                    SetOrder(britr->mLead, orderId, clientId, price, volume, varText);
                }
                else
                {
                    restingLoc = PopSetOrder(orderId, clientId, price, volume, varText);
                    mMem.mOrderPool[britr->mEnd].mNext = restingLoc;
                    britr->mEnd = newLoc;
                }
            }
            else
            {
                resetingLoc = PopSetOrder(orderId, clientId, price, volume, varText);
                supporting.emplace(britr, {restingLoc, newLoc});
            }
        }
        else
        {
            mClient.Handle(BookDeleteInd{mBookId, clientId, orderId});
        }
        return restingLoc;
    }

    size_t ProcessAmend(Order& order, size_t orderOffset, int64_t newPrice, int64_t newVolume, 
        const char& varText[VAR_TEXT_SIZE])
    {
        int64_t adjVolume = volumeDelta ? order.mVolume + newVolume : newVolume;
        bool qpLoss = (adjVolume > order.mVolume);
        bool newPrice = (newPrice != 0);
        bool resetOrderId = qpLoss || newPrice || (!(mBehaviour & AMEND_SAMEQP_SAMEID));
        uint64_t newOrderId = NextOrderId();

        BookAmendInd amendInd;
        amendInd.mBookId       =  mBookId;
        amendInd.mClientId     =  order.mClientId;
        amendInd.mOrigOrderId  =  order.mOrderId;
        amendInd.mNewOrderId   =  resetOrderId ? newOrderId : orderId;
        amendInd.mPrice        =  newPrice;
        amendInd.mVolume       =  newVolume;
        amendInd.mVolumeDelta  =  newVolumeDelta;
        mClient.Handle(amendInd);

        if(qpLoss || newPrice)
        {
            bool isBid = !mBids.empty() && (order.mPrice <= mMem.mOrderPool[mBids.front().mLead]);

            ProcessDelete(order);
            
            if(isBid)
            {
                orderOffset = ProcessInsertSide(newOrderId, amendInd.mClientId, price, 
                    volume, IS_BID, varText, mBids, mAsks, std::less);
            }
            else
            {
                orderOffset = ProcessInsertSide(newOrderId, amendInd.mClientId, price, 
                    volume, IS_ASK, varText, mAsks, mBids, std::greater);
            }
        }
        else if(!resetOrderId)
        {
            order.mVolume = adjVolume;
            memcmp(mOrderExtraInfoPool[orderOffset].mVarText, varText, VAR_TEXT_SIZE);
        }
        else
        {
            order.mVolume = adjVolume;
            order.mOrderId = newOrderId;
            memcmp(mOrderExtraInfoPool[orderOffset].mVarText, varText, VAR_TEXT_SIZE);
        }
        return orderOffset;
    }

    void ProcessQuotes(std::vector<uint64_t>& curQuotes, uint16_t clientId, bool isBid,
            const char& varText[VAR_TEXT_SIZE], const QuoteLevel* levels, uint8_t levelCount)
    {
        uint8_t cnt = 0;
        auto curIdItr = curQuotes.begin();
        while(curIdItr == curQuotes->end())
        {
            // TODO later consider adding insert here if order has been
            // delete or traded otherwise quote adjustments might result 
            // in loss of qp
            auto litr = mMem.mOrderLookup.find(req.mOrderId);
            if(litr == mMem.mOrderLookup.end())
            {
                curIdItr = curQuotes->erase(curIdItr);
                continue;
            }

            auto& order = mMem.mOrderPool[litr->second];
            const auto& level = levels[cnt];
            if(cnt < levelCount)
            {
                ProcessAmend(order, litr->second, level.mPrice, level.mVolume, varText);
                ++curIdItr;
            }
            else
            {
                ProcessDelete(order);
                curIdItr = curQuotes->erase(curIdItr);
            }
            ++cnt;
        }

        while(cnt < levelCount)
        {
            const auto& level = levels[cnt++];
            uint64_t orderId = NextOrderId();

            BookInsertInd insertInd;
            insertInd.mBookId    =  mBookId;
            insertInd.mClientId  =  clientId;
            insertInd.mOrderId   =  orderId;
            insertInd.mFlags     =  isBid ? IS_BID : IS_ASK;
            insertInd.mPrice     =  level.mPrice;
            insertInd.mVolume    =  level.mVolume;
            mClient.Handle(insertInd);
            
            if(isBid)
            {
                ProcessInsertSide(orderId, clientId, level.mPrice, level.mVolume, IS_BID, 
                    varText, mBids, mAsks, std::less);
            }
            else
            {
                ProcessInsertSide(newOrderId, clientId, level.mPrice, level.mVolume, IS_ASK, 
                    varText, mAsks, mBids, std::greater);
            }
            curQuotes.push_back(orderId);
        }
    }

    void ClearReq(const BookInsertReq& req)
    {
        // todo
    }

    void InsertReq(const BookInsertReq& req)
    {
        uint64_t orderId = NextOrderId();

        BookInsertInd insertInd;
        insertInd.mBookId    =  mBookId;
        insertInd.mClientId  =  req.mClientId;
        insertInd.mOrderId   =  orderId;
        insertInd.mFlags     =  req.mFlags;
        insertInd.mPrice     =  req.mPrice;
        insertInd.mVolume    =  req.mVolume;
        mClient.Handle(insertInd);

        if(req.mFlags & IS_BID)
        {
            ProcessInsertSide(orderId, req.mClientId, req.mPrice, req.mVolume, req.mFlags, req.mVarText, mBids, mAsks, std::less);
        }
        else
        {
            ProcessInsertSide(orderId, req.mClientId, req.mPrice, req.mVolume, req.mFlags, req.mVarText, mAsks, mBids, std::greater);
        }
    }

    void QuoteReq(BookQuoteReq& req)
    {
        // todo check
        // prices are descending and not in cross
        // do not modify other participants orders
        auto& curQuotes = mClientQuotes[req.mClientId];
        ProcessQuotes(curQuotes, req.mClientId, true/*isbid*/, req.mVarText, req.mLevels, req.mBids);
        ProcessQuotes(curQuotes, req.mClientId, false/*isbid*/, req.mVarText, req.mLevels+req.mBids, req.mAsks);
    }

    void DeleteReq(BookDeleteReq& req)
    {
        auto litr = mMem.mOrderLookup.find(req.mOrderId);
        if(litr == mMem.mOrderLookup.end())
        {
            mClient.Handle(BookErrorInd{mBookId, req.mClientId, req.mOrderId, UNKNOWN_ORDER});
            return;
        }
        assert(mMem.mOrderPool.size() < litr->second);

        auto& order = mMem.mOrderPool[litr->second];
        if(order.mClientId != req.mClientId)
        {
            mClient.Handle(BookErrorInd{mBookId, req.mClientId, req.mOrderId, NOT_CLIENT_ORDER});
            return;
        }

        ProcessDelete(order);
    }
    
    inline bool MatchVarText(const char& pattern, const char& target)
    {
        return strncmp(VAR_TEXT_SIZE, pattern, target) == 0;
    }

    void BulkDeleteReq(BookBulkDeleteReq& req)
    {
        auto colitr = mMem.mClientOrderLookup.find(req.mClientId);
        if(colitr == mMem.mClientOrderLookup.end())
        {
            mClient.Handle(BookErrorInd{mBookId, req.mClientId, req.mOrderId, CLIENT_HAS_NO_ORDERS});
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

            if(MatchVarText(req.mVarText, mMem.mOrderExtraInfoPool[litr->second].mVarText))
            {
                auto& order = mMem.mOrderPool[litr->second];
                if(order.mFlags & req.mFlags)
                {
                    ProcessDelete(order);
                }
            }
            ++orderIdItr;
        }
    }

    void AmendReq(BookAmendReq& req)
    {
        auto litr = mMem.mOrderLookup.find(req.mOrderId);
        if(litr == mMem.mOrderLookup.end())
        {
            mClient.Handle(BookErrorInd{mBookId, req.mClientId, req.mOrderId, UNKNOWN_ORDER});
            return;
        }
        assert(mMem.mOrderPool.size() < litr->second);

        auto& order = mMem.mOrderPool[litr->second];
        if(order.mClientId != req.mClientId)
        {
            mClient.Handle(BookErrorInd{mBookId, req.mClientId, req.mOrderId, NOT_CLIENT_ORDER});
        }

        ProcessAmend(order, litr->second, req.mPrice, req.mVolume, req.mVarText);
    }

    std::vector<Level> mBids; // offset to start and end of level
    std::vector<Level> mAsks;
    std::dense_hash_map<uint16_t, std::pair<std::vector<uint64_t>, std::vector<uint64_t>>> mClientQuotes;
};

}
