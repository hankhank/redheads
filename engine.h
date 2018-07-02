
#include "book.h"

namespace redheads
{

#pragma pack(1)

enum class EngMsgId : uint8_t
{
    PART_BOOK_CREATE_REQ,

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

inline uint32_t EngSeriesId2InstrType(const EngSeriesId& id)
{
    return 
    (
        (id.mCountry << 16) |
        (id.mMarket << 8)   |
        (id.InstrumentGroup)
    );
}

inline uint64_t EngSeriesId2InstrClass(const EngSeriesId& id)
{
    return 
    (
        (id.mCommodity << 24) |
        (id.mCountry << 16)   |
        (id.mMarket << 8)     |
        (id.InstrumentGroup)
    );
}

inline uint64_t EngSeriesId2Underlying(const EngSeriesId& id)
{
    return 
    (
        (id.mCommodity << 24) |
        (id.mCountry << 16)   |
        (id.mMarket << 8)
    );
}

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
    BookBehaviours mBookBehaviours;
};

struct EngAvailableBooksInd
{
    EngMsgId       mMsgId;
    EngSeriesId    mSeries;
    BookBehaviours mBookBehaviours;
    uint64_t       mTimestamp;
};

struct EngOperationReq
{
    EngMsgId     mMsgId;
    EngSeriesId  mSeries;
    OperationId  mOperationId;
    union
    {
        EngCreateBookReq  mCreateBookReq;
        BookInsertReq     mBookInsertReq;
        BookQuoteReq      mBookQuoteReq;
        BookDeleteReq     mBookDeleteReq;
        BookBulkDeleteReq mBookBulkDeleteReq;
        BookAmendReq      mBookAmendReq;
    };
};

struct EngOperationCnf
{
    EngMsgId mMsgId;
    OperationId  mOperationId;
};

#pragma pop()

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
        if(opId->mSequence > mLastOpId+1) return;
        if(opId->mSequence <= mLastOpId) return;

        auto* books = LookupBook(*series);

        // TODO check sizes here for types
#define HandleBookReq(__ID, __TYPE_REQ)  \
        case __ID: \
        { \
            const auto* req = static_cast<const Book # __TYPE_REQ*>((char*)opId+sizeof(*opId)); \
            for(auto* book : books) book-> __TYPE_REQ(req); \
        }

        if
        (
            books.size() > 1 && 
            ((*opId != PART_BOOK_OP_BULK_DEL_REQ) ||
            (*opId != PART_BOOK_OP_BULK_DEL_REQ))
        )
        {
            return;
        }

        switch(msgId)
        {
            default: return;

            case PART_BOOK_CREATE_REQ:
            {

                size_t newBook = mBooks.size();
                const auto* req = static_cast<const *EngCreateBookReq>((char*)opId+sizeof(*opId));
                mBooks.emplace_back(
                    Book{req->mBookBehaviours, ++mBookId, 0, mBookMem, *this});
                if(mSeriesBookLookup.find(req->mSeries) != mSeriesBookLookup.end())
                {
                    //todo error
                    return;
                }
                mSeriesBookLookup[req->mSeries] = newBook;
                mInstrumentTypeBookLookup[EngSeriesId2InstrType(req->mSeries)].push_back(newBook);
                mInstrumentClassBookLookup[EngSeriesId2InstrClass(req->mSeries)].push_back(newBook);
                mUnderlyingBookLookup[EngSeriesId2Underlying(req->mSeries)].push_back(newBook);
                Send(EngAvailableBooksInd{PART_BOOK_AVAIL_IND, req->mSeries, req->mBookBehaviours, 0});
            }
            break;

            HandleBookReq(PART_BOOK_OP_INSERT_REQ,   InsertReq);
            HandleBookReq(PART_BOOK_OP_QUOTE_REQ,    QuoteReq);
            HandleBookReq(PART_BOOK_OP_DEL_REQ,      DeleteReq);
            HandleBookReq(PART_BOOK_OP_BULK_DEL_REQ, BulkDeleteReq);
            HandleBookReq(PART_BOOK_OP_AMEND_REQ,    AmendReq);
        }
        mLastOpId = opId->mSequence;
        Send(EngOperationCnf{PART_OP_CNF, *opId});
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

    SharedBookMem mBookMem;
    std::vector<Book> mBooks
    std::dense_hash_map<EngSeriesId, size_t> mSeriesBookLookup;
    std::dense_hash_map<uint32_t, std::vector<size_t>> mInstrumentTypeBookLookup;
    std::dense_hash_map<uint64_t, std::vector<size_t>> mInstrumentClassBookLookup;
    std::dense_hash_map<uint32_t, std::vector<size_t>> mUnderlyingBookLookup;

};

}
