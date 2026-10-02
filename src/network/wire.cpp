#include "network/wire.h"

#include <cstring>

namespace openads::network {

bool valid_opcode(std::uint8_t opcode) noexcept {
    switch (opcode) {
        case static_cast<std::uint8_t>(Opcode::Hello):
        case static_cast<std::uint8_t>(Opcode::HelloAck):
        case static_cast<std::uint8_t>(Opcode::Connect):
        case static_cast<std::uint8_t>(Opcode::ConnectAck):
        case static_cast<std::uint8_t>(Opcode::Disconnect):
        case static_cast<std::uint8_t>(Opcode::OpenTable):
        case static_cast<std::uint8_t>(Opcode::OpenTableAck):
        case static_cast<std::uint8_t>(Opcode::CloseTable):
        case static_cast<std::uint8_t>(Opcode::CloseTableAck):
        case static_cast<std::uint8_t>(Opcode::ExecuteSQL):
        case static_cast<std::uint8_t>(Opcode::ExecuteSQLAck):
        case static_cast<std::uint8_t>(Opcode::Fetch):
        case static_cast<std::uint8_t>(Opcode::FetchAck):
        case static_cast<std::uint8_t>(Opcode::GotoTop):
        case static_cast<std::uint8_t>(Opcode::GotoTopAck):
        case static_cast<std::uint8_t>(Opcode::Skip):
        case static_cast<std::uint8_t>(Opcode::SkipAck):
        case static_cast<std::uint8_t>(Opcode::GetField):
        case static_cast<std::uint8_t>(Opcode::GetFieldAck):
        case static_cast<std::uint8_t>(Opcode::GetRecordCount):
        case static_cast<std::uint8_t>(Opcode::GetRecordCountAck):
        case static_cast<std::uint8_t>(Opcode::AtEOF):
        case static_cast<std::uint8_t>(Opcode::AtEOFAck):
        case static_cast<std::uint8_t>(Opcode::DescribeTable):
        case static_cast<std::uint8_t>(Opcode::DescribeTableAck):
        case static_cast<std::uint8_t>(Opcode::AtBOF):
        case static_cast<std::uint8_t>(Opcode::AtBOFAck):
        case static_cast<std::uint8_t>(Opcode::GetRecordNum):
        case static_cast<std::uint8_t>(Opcode::GetRecordNumAck):
        case static_cast<std::uint8_t>(Opcode::IsRecordDeleted):
        case static_cast<std::uint8_t>(Opcode::IsRecordDeletedAck):
        case static_cast<std::uint8_t>(Opcode::GotoBottom):
        case static_cast<std::uint8_t>(Opcode::GotoBottomAck):
        case static_cast<std::uint8_t>(Opcode::IsFound):
        case static_cast<std::uint8_t>(Opcode::IsFoundAck):
        case static_cast<std::uint8_t>(Opcode::RefreshRecord):
        case static_cast<std::uint8_t>(Opcode::RefreshRecordAck):
        case static_cast<std::uint8_t>(Opcode::GetTableType):
        case static_cast<std::uint8_t>(Opcode::GetTableTypeAck):
        case static_cast<std::uint8_t>(Opcode::GetRecordLength):
        case static_cast<std::uint8_t>(Opcode::GetRecordLengthAck):
        case static_cast<std::uint8_t>(Opcode::GetNumIndexes):
        case static_cast<std::uint8_t>(Opcode::GetNumIndexesAck):
        case static_cast<std::uint8_t>(Opcode::GetLastAutoinc):
        case static_cast<std::uint8_t>(Opcode::GetLastAutoincAck):
        case static_cast<std::uint8_t>(Opcode::LockRecord):
        case static_cast<std::uint8_t>(Opcode::LockRecordAck):
        case static_cast<std::uint8_t>(Opcode::UnlockRecord):
        case static_cast<std::uint8_t>(Opcode::UnlockRecordAck):
        case static_cast<std::uint8_t>(Opcode::LockTable):
        case static_cast<std::uint8_t>(Opcode::LockTableAck):
        case static_cast<std::uint8_t>(Opcode::UnlockTable):
        case static_cast<std::uint8_t>(Opcode::UnlockTableAck):
        case static_cast<std::uint8_t>(Opcode::PackTable):
        case static_cast<std::uint8_t>(Opcode::PackTableAck):
        case static_cast<std::uint8_t>(Opcode::ZapTable):
        case static_cast<std::uint8_t>(Opcode::ZapTableAck):
        case static_cast<std::uint8_t>(Opcode::FlushFileBuffers):
        case static_cast<std::uint8_t>(Opcode::FlushFileBuffersAck):
        case static_cast<std::uint8_t>(Opcode::CloseAllIndexes):
        case static_cast<std::uint8_t>(Opcode::CloseAllIndexesAck):
        case static_cast<std::uint8_t>(Opcode::SetAOF):
        case static_cast<std::uint8_t>(Opcode::SetAOFAck):
        case static_cast<std::uint8_t>(Opcode::ClearAOFRemote):
        case static_cast<std::uint8_t>(Opcode::ClearAOFRemoteAck):
        case static_cast<std::uint8_t>(Opcode::GetAOFOptLevel):
        case static_cast<std::uint8_t>(Opcode::GetAOFOptLevelAck):
        case static_cast<std::uint8_t>(Opcode::OpenIndex):
        case static_cast<std::uint8_t>(Opcode::OpenIndexAck):
        case static_cast<std::uint8_t>(Opcode::CloseIndex):
        case static_cast<std::uint8_t>(Opcode::CloseIndexAck):
        case static_cast<std::uint8_t>(Opcode::SetOrder):
        case static_cast<std::uint8_t>(Opcode::SetOrderAck):
        case static_cast<std::uint8_t>(Opcode::SetOrderByName):
        case static_cast<std::uint8_t>(Opcode::SetOrderByNameAck):
        case static_cast<std::uint8_t>(Opcode::Seek):
        case static_cast<std::uint8_t>(Opcode::SeekAck):
        case static_cast<std::uint8_t>(Opcode::SeekLast):
        case static_cast<std::uint8_t>(Opcode::SeekLastAck):
        case static_cast<std::uint8_t>(Opcode::CreateIndex):
        case static_cast<std::uint8_t>(Opcode::CreateIndexAck):
        case static_cast<std::uint8_t>(Opcode::SkipUnique):
        case static_cast<std::uint8_t>(Opcode::SkipUniqueAck):
        case static_cast<std::uint8_t>(Opcode::SetScope):
        case static_cast<std::uint8_t>(Opcode::SetScopeAck):
        case static_cast<std::uint8_t>(Opcode::ClearScope):
        case static_cast<std::uint8_t>(Opcode::ClearScopeAck):
        case static_cast<std::uint8_t>(Opcode::FetchCurrentRow):
        case static_cast<std::uint8_t>(Opcode::FetchCurrentRowAck):
        case static_cast<std::uint8_t>(Opcode::AppendBlank):
        case static_cast<std::uint8_t>(Opcode::AppendBlankAck):
        case static_cast<std::uint8_t>(Opcode::SetField):
        case static_cast<std::uint8_t>(Opcode::SetFieldAck):
        case static_cast<std::uint8_t>(Opcode::SetFields):
        case static_cast<std::uint8_t>(Opcode::SetFieldsAck):
        case static_cast<std::uint8_t>(Opcode::DeleteRecord):
        case static_cast<std::uint8_t>(Opcode::DeleteRecordAck):
        case static_cast<std::uint8_t>(Opcode::RecallRecord):
        case static_cast<std::uint8_t>(Opcode::RecallRecordAck):
        case static_cast<std::uint8_t>(Opcode::GotoRecord):
        case static_cast<std::uint8_t>(Opcode::GotoRecordAck):
        case static_cast<std::uint8_t>(Opcode::FlushTable):
        case static_cast<std::uint8_t>(Opcode::FlushTableAck):
        case static_cast<std::uint8_t>(Opcode::GetKeyType):
        case static_cast<std::uint8_t>(Opcode::GetKeyTypeAck):
        case static_cast<std::uint8_t>(Opcode::Reindex):
        case static_cast<std::uint8_t>(Opcode::ReindexAck):
        case static_cast<std::uint8_t>(Opcode::GetLastTableUpdate):
        case static_cast<std::uint8_t>(Opcode::GetLastTableUpdateAck):
        case static_cast<std::uint8_t>(Opcode::IsRecordLocked):
        case static_cast<std::uint8_t>(Opcode::IsRecordLockedAck):
        case static_cast<std::uint8_t>(Opcode::GetAllLocks):
        case static_cast<std::uint8_t>(Opcode::GetAllLocksAck):
        case static_cast<std::uint8_t>(Opcode::MgConnect):
        case static_cast<std::uint8_t>(Opcode::MgConnectAck):
        case static_cast<std::uint8_t>(Opcode::MgRequest):
        case static_cast<std::uint8_t>(Opcode::MgReplyAck):
        case static_cast<std::uint8_t>(Opcode::FetchWhere):
        case static_cast<std::uint8_t>(Opcode::FetchWhereAck):
        case static_cast<std::uint8_t>(Opcode::Aggregate):
        case static_cast<std::uint8_t>(Opcode::AggregateAck):
        case static_cast<std::uint8_t>(Opcode::GetRecord):
        case static_cast<std::uint8_t>(Opcode::GetRecordAck):
        case static_cast<std::uint8_t>(Opcode::SetRecord):
        case static_cast<std::uint8_t>(Opcode::SetRecordAck):
        case static_cast<std::uint8_t>(Opcode::CustomizeAOF):
        case static_cast<std::uint8_t>(Opcode::CustomizeAOFAck):
        case static_cast<std::uint8_t>(Opcode::GetRecordCRC):
        case static_cast<std::uint8_t>(Opcode::GetRecordCRAck):
        case static_cast<std::uint8_t>(Opcode::GetKeyCount):
        case static_cast<std::uint8_t>(Opcode::GetKeyCountAck):
        case static_cast<std::uint8_t>(Opcode::GetKeyNum):
        case static_cast<std::uint8_t>(Opcode::GetKeyNumAck):
        case static_cast<std::uint8_t>(Opcode::FindTables):
        case static_cast<std::uint8_t>(Opcode::FindTablesAck):
        case static_cast<std::uint8_t>(Opcode::BeginTransaction):
        case static_cast<std::uint8_t>(Opcode::BeginTransactionAck):
        case static_cast<std::uint8_t>(Opcode::CommitTransaction):
        case static_cast<std::uint8_t>(Opcode::CommitTransactionAck):
        case static_cast<std::uint8_t>(Opcode::RollbackTransaction):
        case static_cast<std::uint8_t>(Opcode::RollbackTransactionAck):
        case static_cast<std::uint8_t>(Opcode::FindRecord):
        case static_cast<std::uint8_t>(Opcode::FindRecordAck):
        case static_cast<std::uint8_t>(Opcode::ZipArchive):
        case static_cast<std::uint8_t>(Opcode::ZipArchiveAck):
        case static_cast<std::uint8_t>(Opcode::UnzipArchive):
        case static_cast<std::uint8_t>(Opcode::UnzipArchiveAck):
        case static_cast<std::uint8_t>(Opcode::ZipList):
        case static_cast<std::uint8_t>(Opcode::ZipListAck):
        case static_cast<std::uint8_t>(Opcode::DDGetProperty):
        case static_cast<std::uint8_t>(Opcode::DDGetPropertyAck):
        case static_cast<std::uint8_t>(Opcode::DDSetProperty):
        case static_cast<std::uint8_t>(Opcode::DDSetPropertyAck):
        case static_cast<std::uint8_t>(Opcode::DDCreateProc):
        case static_cast<std::uint8_t>(Opcode::DDCreateProcAck):
        case static_cast<std::uint8_t>(Opcode::DDCreateFunction):
        case static_cast<std::uint8_t>(Opcode::DDCreateFunctionAck):
        case static_cast<std::uint8_t>(Opcode::DDCreateTrigger):
        case static_cast<std::uint8_t>(Opcode::DDCreateTriggerAck):
        case static_cast<std::uint8_t>(Opcode::DDDropTrigger):
        case static_cast<std::uint8_t>(Opcode::DDDropTriggerAck):
        case static_cast<std::uint8_t>(Opcode::DDDropView):
        case static_cast<std::uint8_t>(Opcode::DDDropViewAck):
        case static_cast<std::uint8_t>(Opcode::DDDropLink):
        case static_cast<std::uint8_t>(Opcode::DDDropLinkAck):
        case static_cast<std::uint8_t>(Opcode::DDCreateUser):
        case static_cast<std::uint8_t>(Opcode::DDCreateUserAck):
        case static_cast<std::uint8_t>(Opcode::DDDropObject):
        case static_cast<std::uint8_t>(Opcode::DDDropObjectAck):
        case static_cast<std::uint8_t>(Opcode::DDAddUserToGroup):
        case static_cast<std::uint8_t>(Opcode::DDAddUserToGroupAck):
        case static_cast<std::uint8_t>(Opcode::DDRemoveUserFromGroup):
        case static_cast<std::uint8_t>(Opcode::DDRemoveUserFromGroupAck):
        case static_cast<std::uint8_t>(Opcode::DDCreateLink):
        case static_cast<std::uint8_t>(Opcode::DDCreateLinkAck):
        case static_cast<std::uint8_t>(Opcode::DDModifyLink):
        case static_cast<std::uint8_t>(Opcode::DDModifyLinkAck):
        case static_cast<std::uint8_t>(Opcode::DDCreateRefIntegrity):
        case static_cast<std::uint8_t>(Opcode::DDCreateRefIntegrityAck):
        case static_cast<std::uint8_t>(Opcode::DDCreateView):
        case static_cast<std::uint8_t>(Opcode::DDCreateViewAck):
        case static_cast<std::uint8_t>(Opcode::DDAddIndexFile):
        case static_cast<std::uint8_t>(Opcode::DDAddIndexFileAck):
        case static_cast<std::uint8_t>(Opcode::DDRemoveIndexFile):
        case static_cast<std::uint8_t>(Opcode::DDRemoveIndexFileAck):
        case static_cast<std::uint8_t>(Opcode::DDGetPermissions):
        case static_cast<std::uint8_t>(Opcode::DDGetPermissionsAck):
        case static_cast<std::uint8_t>(Opcode::DDGrantPermission):
        case static_cast<std::uint8_t>(Opcode::DDGrantPermissionAck):
        case static_cast<std::uint8_t>(Opcode::ShowDeleted):
        case static_cast<std::uint8_t>(Opcode::ShowDeletedAck):
        case static_cast<std::uint8_t>(Opcode::CreateTable):
        case static_cast<std::uint8_t>(Opcode::CreateTableAck):
        case static_cast<std::uint8_t>(Opcode::DropTable):
        case static_cast<std::uint8_t>(Opcode::DropTableAck):
        case static_cast<std::uint8_t>(Opcode::FileExists):
        case static_cast<std::uint8_t>(Opcode::FileExistsAck):
        case static_cast<std::uint8_t>(Opcode::FileErase):
        case static_cast<std::uint8_t>(Opcode::FileEraseAck):
        case static_cast<std::uint8_t>(Opcode::FileRename):
        case static_cast<std::uint8_t>(Opcode::FileRenameAck):
        case static_cast<std::uint8_t>(Opcode::FileSize):
        case static_cast<std::uint8_t>(Opcode::FileSizeAck):
        case static_cast<std::uint8_t>(Opcode::FileMTime):
        case static_cast<std::uint8_t>(Opcode::FileMTimeAck):
        case static_cast<std::uint8_t>(Opcode::Directory):
        case static_cast<std::uint8_t>(Opcode::DirectoryAck):
        case static_cast<std::uint8_t>(Opcode::DirExist):
        case static_cast<std::uint8_t>(Opcode::DirExistAck):
        case static_cast<std::uint8_t>(Opcode::DirMake):
        case static_cast<std::uint8_t>(Opcode::DirMakeAck):
        case static_cast<std::uint8_t>(Opcode::DirRemove):
        case static_cast<std::uint8_t>(Opcode::DirRemoveAck):
        case static_cast<std::uint8_t>(Opcode::FOpen):
        case static_cast<std::uint8_t>(Opcode::FOpenAck):
        case static_cast<std::uint8_t>(Opcode::FCreate):
        case static_cast<std::uint8_t>(Opcode::FCreateAck):
        case static_cast<std::uint8_t>(Opcode::FClose):
        case static_cast<std::uint8_t>(Opcode::FCloseAck):
        case static_cast<std::uint8_t>(Opcode::FRead):
        case static_cast<std::uint8_t>(Opcode::FReadAck):
        case static_cast<std::uint8_t>(Opcode::FWrite):
        case static_cast<std::uint8_t>(Opcode::FWriteAck):
        case static_cast<std::uint8_t>(Opcode::FSeek):
        case static_cast<std::uint8_t>(Opcode::FSeekAck):
        case static_cast<std::uint8_t>(Opcode::Mutex):
        case static_cast<std::uint8_t>(Opcode::Error):
            return true;
        default: return false;
    }
}

util::Result<std::vector<std::uint8_t>> encode_frame(const Frame& f) {
    if (f.payload.size() > 0xFFFFFFFFu) {
        return util::Error{5000, 0, "frame payload too large", ""};
    }
    std::vector<std::uint8_t> out;
    out.reserve(5 + f.payload.size());
    std::uint32_t n = static_cast<std::uint32_t>(f.payload.size());
    out.push_back(static_cast<std::uint8_t>((n >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((n >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((n >>  8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>( n        & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(f.opcode));
    if (n > 0) {
        out.insert(out.end(), f.payload.begin(), f.payload.end());
    }
    return out;
}

util::Result<Frame> decode_frame(const std::uint8_t* buf,
                                  std::size_t size,
                                  std::size_t* consumed) {
    if (size < 5) {
        return util::Error{5000, 0, "frame buffer shorter than header", ""};
    }
    std::uint32_t n =
        (static_cast<std::uint32_t>(buf[0]) << 24) |
        (static_cast<std::uint32_t>(buf[1]) << 16) |
        (static_cast<std::uint32_t>(buf[2]) <<  8) |
         static_cast<std::uint32_t>(buf[3]);
    if (n > kMaxFramePayload) {
        return util::Error{5000, 0, "frame payload too large", ""};
    }
    if (size < 5 + static_cast<std::size_t>(n)) {
        return util::Error{5000, 0, "frame buffer truncated", ""};
    }
    if (!valid_opcode(buf[4]))
        return util::Error{5000, 0, "unknown opcode", ""};
    Frame f;
    f.opcode = static_cast<Opcode>(buf[4]);
    if (n > 0) {
        f.payload.assign(buf + 5, buf + 5 + n);
    }
    if (consumed) *consumed = 5 + n;
    return f;
}

} // namespace openads::network
