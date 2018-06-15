
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

    Opcode   mCode;
    uint8_t  mAPID;
    uint16_t mTransId;
    uint16_t mBookId;

    Opcode   mCode;
    uint8_t  mAPID;
    uint16_t mTransId;
    uint16_t mBookId;

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

struct ManagementReq
{
    Opcode mCode;
    uint16_t mBookId;
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
