#include "switch_source.hpp"

#include <switch.h>

#include <cstring>
#include <memory>

#include "lib/nacp.hpp"

namespace lib {
namespace {

constexpr s32 kEventBatch = 1024;

Uid fromAccount(const AccountUid& a) { return Uid{a.uid[0], a.uid[1]}; }

AccountUid toAccount(const Uid& u) {
    AccountUid a;
    a.uid[0] = u.hi;
    a.uid[1] = u.lo;
    return a;
}

// SetLanguage (system settings order) -> NACP language index
int nacpLanguageFor(SetLanguage lang) {
    switch (lang) {
        case SetLanguage_ENUS: return 0;
        case SetLanguage_ENGB: return 1;
        case SetLanguage_JA: return 2;
        case SetLanguage_FR: return 3;
        case SetLanguage_DE: return 4;
        case SetLanguage_ES419: return 5;
        case SetLanguage_ES: return 6;
        case SetLanguage_IT: return 7;
        case SetLanguage_NL: return 8;
        case SetLanguage_FRCA: return 9;
        case SetLanguage_PT: return 10;
        case SetLanguage_RU: return 11;
        case SetLanguage_KO: return 12;
        case SetLanguage_ZHTW:
        case SetLanguage_ZHHANT: return 13;
        case SetLanguage_ZHCN:
        case SetLanguage_ZHHANS: return 14;
        case SetLanguage_PTBR: return 15;
        default: return 0;
    }
}

SystemStats convert(const PdmPlayStatistics& st) {
    SystemStats out;
    out.playSeconds = static_cast<int64_t>(st.playtime / 1000 / 1000 / 1000);   // the system counts nanoseconds
    out.launches = st.total_launches;
    out.first = st.first_timestamp_user;
    out.last = st.last_timestamp_user;
    return out;
}

}  // namespace

SwitchSource::SwitchSource() {
    pdmqryInitialize();
    accountInitialize(AccountServiceType_Application);
    nsInitialize();
    if (R_SUCCEEDED(setInitialize())) {
        u64 code = 0;
        SetLanguage lang = SetLanguage_ENUS;
        if (R_SUCCEEDED(setGetSystemLanguage(&code)) && R_SUCCEEDED(setMakeLanguage(code, &lang))) {
            language_ = nacpLanguageFor(lang);
        }
    }
}

SwitchSource::~SwitchSource() {
    setExit();
    nsExit();
    accountExit();
    pdmqryExit();
}

std::vector<Profile> SwitchSource::profiles() {
    std::vector<Profile> out;
    AccountUid uids[ACC_USER_LIST_SIZE];
    s32 count = 0;
    if (R_FAILED(accountListAllUsers(uids, ACC_USER_LIST_SIZE, &count))) {
        return out;
    }

    for (s32 i = 0; i < count; i++) {
        Profile p;
        p.uid = fromAccount(uids[i]);
        AccountProfile profile;
        if (R_SUCCEEDED(accountGetProfile(&profile, uids[i]))) {
            AccountProfileBase base;
            std::memset(&base, 0, sizeof(base));
            if (R_SUCCEEDED(accountProfileGet(&profile, nullptr, &base))) {
                p.name = std::string(base.nickname, strnlen(base.nickname, sizeof(base.nickname)));
            }
            u32 size = 0;
            if (R_SUCCEEDED(accountProfileGetImageSize(&profile, &size)) && size > 0 && size <= 512 * 1024) {
                p.avatar.resize(size);
                u32 real = 0;
                if (R_FAILED(accountProfileLoadImage(&profile, p.avatar.data(), size, &real)) || real == 0 || real > size) {
                    p.avatar.clear();
                } else {
                    p.avatar.resize(real);
                }
            }
            accountProfileClose(&profile);
        }
        out.push_back(p);
    }
    return out;
}

std::vector<TitleInfo> SwitchSource::titles() {
    std::vector<TitleInfo> out;
    const s32 batch = 64;
    std::unique_ptr<NsApplicationRecord[]> records(new NsApplicationRecord[batch]);
    std::unique_ptr<NsApplicationControlData> data(new NsApplicationControlData);   // ~144 KB, too big for the stack

    s32 offset = 0;
    while (true) {
        s32 count = 0;
        if (R_FAILED(nsListApplicationRecord(records.get(), batch, offset, &count)) || count <= 0) {
            break;
        }
        for (s32 i = 0; i < count; i++) {
            TitleInfo t;
            t.id = records[i].application_id;
            t.installed = true;

            u64 actual = 0;
            std::memset(data.get(), 0, sizeof(NsApplicationControlData));
            if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, t.id, data.get(), sizeof(NsApplicationControlData), &actual)) &&
                actual >= sizeof(NacpStruct)) {
                t.name = nacp::extractName(reinterpret_cast<const uint8_t*>(&data->nacp), sizeof(NacpStruct), language_);
                size_t iconSize = actual - sizeof(NacpStruct);
                if (iconSize > 0 && iconSize <= sizeof(data->icon)) {
                    t.icon.assign(data->icon, data->icon + iconSize);
                }
            }
            out.push_back(std::move(t));
        }
        offset += count;
        if (count < batch) {
            break;
        }
    }
    return out;
}

std::optional<TitleInfo> SwitchSource::uninstalledTitle(TitleId id) {
    // Same call as for installed titles, but cache only: ns keeps control data (name, icon) of
    // titles that were removed, which is where NX Activity Log and the user page get them from
    std::unique_ptr<NsApplicationControlData> data(new NsApplicationControlData);
    std::memset(data.get(), 0, sizeof(NsApplicationControlData));
    u64 actual = 0;
    if (R_FAILED(nsGetApplicationControlData(NsApplicationControlSource_CacheOnly, id, data.get(), sizeof(NsApplicationControlData), &actual)) ||
        actual < sizeof(NacpStruct)) {
        return std::nullopt;
    }
    TitleInfo t;
    t.id = id;
    t.installed = false;
    t.name = nacp::extractName(reinterpret_cast<const uint8_t*>(&data->nacp), sizeof(NacpStruct), language_);
    if (t.name.empty()) return std::nullopt;
    size_t iconSize = actual - sizeof(NacpStruct);
    if (iconSize > 0 && iconSize <= sizeof(data->icon)) t.icon.assign(data->icon, data->icon + iconSize);
    return t;
}

std::vector<RawEvent> SwitchSource::events() {
    std::vector<RawEvent> out;
    std::unique_ptr<PdmPlayEvent[]> batch(new PdmPlayEvent[kEventBatch]);

    s32 offset = 0;
    s32 total = 1;
    while (total > 0) {
        std::memset(batch.get(), 0, kEventBatch * sizeof(PdmPlayEvent));
        if (R_FAILED(pdmqryQueryPlayEvent(offset, batch.get(), kEventBatch, &total)) || total == 0) {
            break;
        }
        offset += total;

        for (s32 i = 0; i < total; i++) {
            const PdmPlayEvent& pe = batch[i];
            RawEvent e;

            if (pe.play_event_type == PdmPlayEventType_Account) {
                if (pe.event_data.account.type == 0) {
                    e.kind = EventKind::AccountActive;
                } else if (pe.event_data.account.type == 1) {
                    e.kind = EventKind::AccountInactive;
                } else {
                    continue;
                }
                // The four 32-bit words of the profile id are stored in an unusual order
                e.user.hi = (static_cast<uint64_t>(pe.event_data.account.uid[0]) << 32) | pe.event_data.account.uid[1];
                e.user.lo = (static_cast<uint64_t>(pe.event_data.account.uid[2]) << 32) | pe.event_data.account.uid[3];
            } else if (pe.play_event_type == PdmPlayEventType_Applet) {
                if (pe.event_data.applet.log_policy != PdmPlayLogPolicy_All) {
                    continue;
                }
                e.title = (static_cast<uint64_t>(pe.event_data.applet.program_id[0]) << 32) | pe.event_data.applet.program_id[1];
                if (e.title == 0) {
                    continue;
                }
                switch (pe.event_data.applet.event_type) {
                    case PdmAppletEventType_Launch: e.kind = EventKind::Launch; break;
                    case PdmAppletEventType_Exit:
                    case PdmAppletEventType_Exit5:
                    case PdmAppletEventType_Exit6: e.kind = EventKind::Exit; break;
                    case PdmAppletEventType_InFocus: e.kind = EventKind::InFocus; break;
                    case PdmAppletEventType_OutOfFocus:
                    case PdmAppletEventType_OutOfFocus4: e.kind = EventKind::OutFocus; break;
                    default: continue;
                }
            } else {
                continue;
            }

            e.clock = static_cast<int64_t>(pe.timestamp_user);
            e.steady = static_cast<int64_t>(pe.timestamp_steady);
            out.push_back(e);
        }
    }
    return out;
}

std::optional<SystemStats> SwitchSource::userStats(TitleId id, const Uid& uid) {
    PdmPlayStatistics st = {};
    if (R_FAILED(pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(id, toAccount(uid), false, &st))) {
        return std::nullopt;
    }
    if (st.playtime == 0 && st.total_launches == 0) {
        return std::nullopt;
    }
    return convert(st);
}

std::optional<SystemStats> SwitchSource::consoleStats(TitleId id) {
    PdmPlayStatistics st = {};
    if (R_FAILED(pdmqryQueryPlayStatisticsByApplicationId(id, false, &st))) {
        return std::nullopt;
    }
    if (st.playtime == 0 && st.total_launches == 0) {
        return std::nullopt;
    }
    return convert(st);
}

TitleId SwitchSource::ownTitleId() {
    u64 id = 0;
    if (R_FAILED(svcGetInfo(&id, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0))) {
        return 0;
    }
    return id;
}

Uid SwitchSource::preselectedUser() {
    AccountUid uid = {};
    if (R_SUCCEEDED(accountInitialize(AccountServiceType_Application))) {
        if (R_FAILED(accountGetPreselectedUser(&uid))) {
            uid = AccountUid{};
        }
        accountExit();
    }
    return fromAccount(uid);
}

int SwitchSource::utcOffsetSeconds() {
    u64 now = 0;
    TimeCalendarTime cal;
    TimeCalendarAdditionalInfo info;
    if (R_FAILED(timeGetCurrentTime(TimeType_UserSystemClock, &now)) || R_FAILED(timeToCalendarTimeWithMyRule(now, &cal, &info))) {
        return 0;
    }
    return info.offset;
}

}  // namespace lib
