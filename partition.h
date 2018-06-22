
#include "book.h"

namespace redheads
{

#pragma pack(1)

enum class PartMsgId : uint8_t
{
    PART_BOOK_CREATE_REQ,
    PART_BOOK_OP_INSERT_REQ,
    PART_BOOK_OP_QUOTE_REQ,
    PART_BOOK_OP_DEL_REQ,
    PART_BOOK_OP_BULK_DEL_REQ,
    PART_BOOK_OP_AMEND_REQ,

    PART_BOOK_AVAIL_IND,
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
struct PartSeriesId
{
    uint8_t  mCountry;
    uint8_t  mMarket;
    uint8_t  mInstrumentGroup;
    uint8_t  mModifier;
    uint16_t mCommodity;
    uint16_t mExpirationDate;
    int32_t  mStrikePrice;
};

// map by total key - single book
// map by underlying - vector of books
// map by instrument class - vector of books
// map by instrument type - vector of books

struct PartCreateBookReq
{
    PartMsgId      mMsgId;
    PartSeriesId   mSeries;
    BookBehaviours mBookBehaviours;
};

struct PartAvailableBooksInd
{
    PartMsgId      mMsgId;
    PartSeriesId   mSeries;
    BookBehaviours mBookBehaviours;
    uint64_t       mTimestamp;
};

struct PartOperationReq
{
    PartMsgId     mMsgId;
    uint16_t      mOperationId;
    PartSeriesId  mSeries;
    union
    {
        PartCreateBookReq mCreateBookReq;
        BookInsertReq     mBookInsertReq;
        BookQuoteReq      mBookQuoteReq;
        BookDeleteReq     mBookDeleteReq;
        BookBulkDeleteReq mBookBulkDeleteReq;
        BookAmendReq      mBookAmendReq;
    };
};

struct PartOperationInd
{
    PartMsgId     mMsgId;
    uint16_t      mOperationId;
    PartSeriesId  mSeries;
    union
    {
        BookInsertInd mBookInsertInd;
        BookDeleteInd mBookDeleteInd;
        BookAmendInd  mBookAmendInd;
        BookTradeInd  mBookTradeInd;
        BookErrorInd  mBookErrorInd;
    };
};

struct PartOperationCnf
{
    PartMsgId mMsgId;
    uint16_t  mOperationId;
};

#pragma pop()

struct Partition : IBookClient
{
    Partition() {}
    
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

    void HandleMsg(const PartOperationReq* req, size_t size)
    {
        PartMsgId msgId = static_cast<PartMsgId>(buf[0]);
        switch(msgId)
        {
        }
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
    std::dense_hash_map<uint64_t, Book> mBookLookup;
};

}
