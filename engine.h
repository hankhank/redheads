
#include "book.h"

namespace redheads
{

#pragma pack(1)

enum class EngMsgId : uint8_t
{
    PART_BOOK_CREATE_REQ,

    PART_BOOK_OP_CLEAR_REQ,
    PART_BOOK_OP_INSERT_REQ,
    PART_BOOK_OP_QUOTE_REQ,
    PART_BOOK_OP_DEL_REQ,
    PART_BOOK_OP_BULK_DEL_REQ,
    PART_BOOK_OP_AMEND_REQ,

    PART_BOOK_AVAIL_IND,
    PART_OP_CNF,
};

// Instrument Type
//   1. Country
//   2. Market code
//   3. Instrument group
// Instrument Class
//   1. Country
//   2. Market code
//   3. Instrument group
//   4. Commodity code
// Underlying
//   1. Country
//   2. Market code
//   3. Commodity code
// Complete Series
//   1. Everything
struct EngSeriesId
{
    uint8_t  mCountry;
    uint8_t  mMarket;
    uint8_t  mInstrumentGroup;
    uint8_t  mModifier;
    uint16_t mCommodity;
    uint16_t mExpirationDate;
    int32_t  mStrikePrice;
};

struct OperationId
{
    uint16_t mGatewayId;
    uint16_t mSequence;
};

struct EngCreateBookReq
{
    EngMsgId       mMsgId;
    EngSeriesId    mSeries;
    OperationId    mOperationId;
    uint16_t       mBookId;
    BookBehaviours mBookBehaviours;
};

struct EngOperationReq
{
    EngMsgId     mMsgId;
    EngSeriesId  mSeries;
    OperationId  mOperationId;
};

struct EngAvailableBooksInd
{
    EngMsgId       mMsgId;
    EngSeriesId    mSeries;
    uint16_t       mBookId;
    BookBehaviours mBookBehaviours;
    uint64_t       mTimestamp;
};

struct EngBookInd
{
    EngMsgId       mMsgId;
};

struct EngOperationCnf
{
    EngMsgId    mMsgId;
    OperationId mOperationId;
};

#pragma pop()

inline EngSeriesId MaskEngSeriesIdByInstrType(EngSeriesId id)
{
    id.mModifier = 0;
    id.mCommodity = 0;
    id.mExpirationDate = 0;
    id.mStrikePrice = 0;
    return id;
}

inline EngSeriesId MaskEngSeriesIdByInstrClass(EngSeriesId id)
{
    id.mModifier = 0;
    id.mExpirationDate = 0;
    id.mStrikePrice = 0;
}

inline EngSeriesId MaskEngSeriesIdByUnderlying(EngSeriesId id)
{
    id.mInstrumentGroup = 0;
    id.mModifier = 0;
    id.mExpirationDate = 0;
    id.mStrikePrice = 0;
}

struct IEngineClient
{
    virtual ~IEngineClient(){}
    virtual void Handle(const BookClearInd&& ind) = 0;
};

struct Engine : IBookClient
{
    Engine() {}
    
    void Init(size_t initLevelAlloc, size_t initOrderAlloc, size_t initClientAlloc)
    {
        assert(mBookMem.mOrderPool.empty() && "Can only init allocate once");
        mOrderLookup.resize(initOrderAlloc);
        mClientOrderLookup.resize(initClientAlloc);
        mOrderExtraInfoPool.reserve(initOrderAlloc);
        mOrderFreeList.reserve(initOrderAlloc);
        mDroppedLevels.reserve(initLevelAlloc);

        for(size_t i = 0; i < initOrderAlloc; ++i) mOrderFreeList.push_back(i);
    }

    void HandleMsg(const char* buf, size_t size)
    {
        const auto* msgId  = static_cast<const EngMsgId*>(buf+0);
        const auto* series = static_cast<const EngSeriesId*>((const char*)msgId+sizeof(*msgId));
        const auto* opId   = static_cast<OperationId*>((const char*)series+sizeof(*series));

        if(size < (sizeof(*msgId) + sizeof(*series) + sizeof(*opId))) return;
        HandleMsg(*msgId, *series, *opId, opId + sizeof(*opId));
    }

    void HandleMsg(const EngOperationReq& req, void* msg)
    {
        if(req.mOperationId.mSequence > mLastOpId+1) return;
        if(req.mOperationId.mSequence <= mLastOpId) return;

        auto* books = LookupBook(series);

        // TODO check sizes here for types
#define HandleBookReq(__ID, __TYPE_REQ)  \
        case __ID: \
        { \
            const auto* req = static_cast<const Book # __TYPE_REQ*>(req.mMsg); \
            for(auto* book : books) book-> __TYPE_REQ(*req); \
        }

        if
        (
            books.size() > 1 && 
            ((id != PART_BOOK_OP_BULK_DEL_REQ) ||
            (id != PART_BOOK_OP_BULK_DEL_REQ))
        )
        {
            return;
        }

        switch(req.mMsgId)
        {
            default: return;

            case PART_BOOK_CREATE_REQ:
            {
                size_t newBook = mBooks.size();
                const auto* req = static_cast<const *EngCreateBookReq>((char*)opId+sizeof(*opId));
                mBooks.emplace_back(
                    Book{req->mBookBehaviours, req->mBookId, 0, mBookMem, *this});
                if(mSeriesBookLookup.find(req->mSeries) != mSeriesBookLookup.end())
                {
                    //todo error
                    return;
                }
                mSeriesBookLookup[req->mSeries] = newBook;
                mSeriesBookLookup[MaskEngSeriesIdByInstrType(req->mSeries)].push_back(newBook);
                mSeriesBookLookup[MaskEngSeriesIdByInstrClass(req->mSeries)].push_back(newBook);
                mSeriesBookLookup[MaskEngSeriesIdByUnderlying(req->mSeries)].push_back(newBook);
                Handle(EngAvailableBooksInd{PART_BOOK_AVAIL_IND, req->mSeries, req->mBookBehaviours, 0});
            }
            break;

            HandleBookReq(PART_BOOK_OP_CLEAR_REQ,    ClearReq);
            HandleBookReq(PART_BOOK_OP_INSERT_REQ,   InsertReq);
            HandleBookReq(PART_BOOK_OP_QUOTE_REQ,    QuoteReq);
            HandleBookReq(PART_BOOK_OP_DEL_REQ,      DeleteReq);
            HandleBookReq(PART_BOOK_OP_BULK_DEL_REQ, BulkDeleteReq);
            HandleBookReq(PART_BOOK_OP_AMEND_REQ,    AmendReq);
        }
        mLastOpId = req.mOperationId.mSequence;
        Handle(EngOperationCnf{PART_OP_CNF, req.mOperationId});
    }

    void ImmediateCleanup()
    {
        for(auto leadingLoc : mDroppedLevels)
        {
            size_t nextLoc = leadingLoc->mLead;
            do
            {
                mOrderFreeList.push_back(nextLoc);
                size_t nextNextLoc = mMem.mOrderPool[nextLoc].mNext;
                mMem.mOrderPool[nextLoc] = Order();
                nextOrder = nextNextLoc;
            }
            while(nextLoc);
        }

        if(mOrderFreeList.empty())
        {
            size_t origSize = mMem.mOrderPool.size();
            mMem.mOrderPool.resize(2*mMem.mOrderPool.size());
            for(size_t i = origSize; i < 2*origSize; ++i) mOrderFreeList.push_back(i);
        }
    }

    SharedBookMem mBookMem;
    std::vector<Book> mBooks
    std::dense_hash_map<EngSeriesId, size_t> mSeriesBookLookup;
};

}
