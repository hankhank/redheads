
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
