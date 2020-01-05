#include <Windows.h>
#include <stdint.h>

#define CQ_MAX_RECORD_COUNT 100

#define CQRecord_Size 720
struct CQRecord {
    uint64_t qq_id; // QQ号
    uint64_t group_id; //群号，如果为0表示不是群消息，否则为群或讨论组消息
    char user_name[64]; //昵称
    char remark_name[64]; //备注
    char group_name[64]; //群名，如果不为空则表示是群或讨论组消息
    char message[512]; //消息
};

#define CQSharedMemory_Size 72008
struct CQSharedMemory {
    uint32_t current_pos;
    uint32_t count;
    CQRecord records[CQ_MAX_RECORD_COUNT];
};

#define CQ_MAPPING_NAME "CQSharedMemory"

HANDLE _cq_hMap=NULL;
CQSharedMemory* _cq_pSharedMemory=NULL;

BOOL CQInitSharedMemory() {
    _cq_hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, CQ_MAPPING_NAME);
    if (!_cq_hMap) {
        _cq_hMap =
            CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, CQSharedMemory_Size, CQ_MAPPING_NAME);
        if (!_cq_hMap) return FALSE;
    }
    _cq_pSharedMemory = (CQSharedMemory*)MapViewOfFile(_cq_hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (_cq_pSharedMemory == nullptr) return FALSE;
    _cq_pSharedMemory->current_pos = _cq_pSharedMemory->count = 0;
    return TRUE;
}

BOOL CQUninitSharedMemory() {
    if (!UnmapViewOfFile(_cq_pSharedMemory)) return FALSE;
    _cq_pSharedMemory = NULL;
    if (!CloseHandle(_cq_hMap)) return FALSE;
    _cq_hMap = NULL;
    return TRUE;
}

BOOL CQIsSharedMemoryInited() { return _cq_hMap && _cq_pSharedMemory; }

//remark_name, group_name 可以为空
//group_id 为0表示是好友消息，否则是群消息
CQRecord& CQAddRecord(uint64_t qq_id, uint64_t group_id, LPCSTR user_name, LPCSTR remark_name, LPCSTR group_name,
                      LPCSTR message) {
    CQRecord& r = _cq_pSharedMemory->records[_cq_pSharedMemory->current_pos];
    r.qq_id = qq_id;
    r.group_id = group_id;
    strncpy_s(r.user_name, user_name, ARRAYSIZE(r.user_name) - 1);
    if (remark_name)
        strncpy_s(r.remark_name, remark_name, ARRAYSIZE(r.remark_name) - 1);
    else
        r.remark_name[0] = 0;
    if (group_name)
        strncpy_s(r.group_name, group_name, ARRAYSIZE(r.group_name) - 1);
    else
        r.group_name[0] = 0;
    strncpy_s(r.message, message, ARRAYSIZE(r.message) - 1);
    _cq_pSharedMemory->count++;
    if (_cq_pSharedMemory->count > CQ_MAX_RECORD_COUNT) _cq_pSharedMemory->count = CQ_MAX_RECORD_COUNT;
    _cq_pSharedMemory->current_pos = (_cq_pSharedMemory->current_pos + 1) % CQ_MAX_RECORD_COUNT;
    return r;
}

CQSharedMemory& CQGetSharedMemory() { return *_cq_pSharedMemory; }