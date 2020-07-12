#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#include <cqcppsdk/cqcppsdk.h>

#include <WS2tcpip.h>
#include "comm.hpp"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ws2_32.lib")

using namespace cq;
using namespace std;

static bool logShowQQ = false;
static bool logMessage = false;
static bool friendAutoReply = true;
std::map<uint64_t, uint32_t> group_msg_count;
std::map<uint64_t, time_t> friend_usage_sent;

int iScheduleTimerID = 0;
tm last_localtm{};

std::string getRecentMessages(uint32_t count, bool send_privacy) {
    std::string msg = "最近" + std::to_string(count) + "条消息：";
    CQSharedMemory &m = CQGetSharedMemory();
    if (count > m.count) msg += "（目前只记录了" + std::to_string(m.count) + "条消息）";
    int start = m.current_pos;
    if (m.count > count)
        start = (start + CQ_MAX_RECORD_COUNT - count) % CQ_MAX_RECORD_COUNT;
    else
        start = (start + CQ_MAX_RECORD_COUNT - m.count) % CQ_MAX_RECORD_COUNT;
    while (start != m.current_pos) {
        msg += "\n";
        CQRecord r = m.records[start];
        if (!send_privacy) {
            r.group_id = r.group_id - r.group_id % 10000;
            if (r.group_name[0]) strcpy(r.group_name + 9, "***");
            strcpy(r.message + 15, "***");
            r.qq_id = r.qq_id - r.qq_id % 10000;
            if (r.remark_name[0]) strcpy(r.remark_name, "***");
            strcpy(r.user_name + 9, "***");
        }
        if (r.group_id) {
            msg += std::string(r.group_name) + ":";
            if (logShowQQ) msg += "(" + std::to_string(r.group_id) + ")";
        }
        std::string name = r.user_name;
        if (strlen(r.remark_name) > 0) name = std::string(r.remark_name) + "(" + std::string(r.user_name) + ")";
        if (logShowQQ) name += "(" + std::to_string(r.qq_id) + ")";
        msg += name + ":" + std::string(r.message);
        msg += start = (start + 1) % CQ_MAX_RECORD_COUNT;
    }
    return msg;
}

std::string lower(std::string str) {
    for (auto &c : str) {
        if (c >= 'A' && c <= 'Z') {
            c = c + 'a' - 'A';
        }
    }
    return str;
}

void EchoCommand(const MessageEvent &e, uint64_t user_id, uint64_t discuss_id, uint64_t group_id) {
    std::string cmd = e.message.substr(5);
    if (group_id)
        send_group_message(group_id, cmd);
    else if (discuss_id)
        send_discuss_message(discuss_id, cmd);
    else
        send_private_message(user_id, cmd);
}

void StopStatScheduleTimer();
void StartStatScheduleTimer();
void ProcessException(const ApiError &ex, const string &msg, uint64_t user_id, uint64_t discuss_id, uint64_t group_id);

string GetErrorMsg(DWORD e) {
    WCHAR msgBufW[80] = L"";

    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL,
                   e,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   msgBufW,
                   ARRAYSIZE(msgBufW),
                   NULL);
    if (msgBufW[0] == 0) return "发生错误。";
    wstring msgStrW = msgBufW;
    size_t pln = msgStrW.find('\n');
    if (pln != string::npos) msgStrW.resize(pln);
    pln = msgStrW.find('\r');
    if (pln != string::npos) msgStrW.resize(pln);
    return utils::ws2s(msgStrW);
}

string ToHex(DWORD e) {
    char buf[20];
    sprintf(buf, "%#x", e);
    return buf;
}

string GetLocalAddress(string protocol) {
    std::smatch sm;
    if (std::regex_search(protocol, sm, std::regex(" +([A-Za-z0-9_\\-]+)")))
        protocol = lower(sm[1].str());
    if (protocol == "http" || protocol == "ftp" || protocol == "https")
        protocol += "://";
    else
        protocol = "";
    string msg;
    char strHost[256] = "";
    if (SOCKET_ERROR == gethostname(strHost, sizeof(strHost))) {
        DWORD e = WSAGetLastError();
        msg.assign(GetErrorMsg(e) + "(" + ToHex(e) + ")");
    } else {
        msg += "主机名：" + string(strHost);
        addrinfo *addr_info = NULL;
        DWORD e = getaddrinfo(strHost, NULL, NULL, &addr_info);
        if (e) {
            msg += "\n" + GetErrorMsg(e) + "(" + ToHex(e) + ")";
        } else {
            for (addrinfo *p = addr_info; p; p = p->ai_next) {
                switch (p->ai_family) {
                case AF_INET:
                    msg += "\n";
                    switch (p->ai_protocol) {
                    case IPPROTO_IP:
                        msg += "IP/";
                        break;
                    case IPPROTO_IPV4:
                        msg += "IPv4/";
                        break;
                    case IPPROTO_IPV6:
                        msg += "IPv6/";
                        break;
                    case IPPROTO_TCP:
                        msg += "TCP/";
                        break;
                    case IPPROTO_UDP:
                        msg += "UDP/";
                        break;
                    }
                    msg += "IPv4: ";
                    char ip_str[INET_ADDRSTRLEN + 1];
                    ZeroMemory(ip_str, sizeof(ip_str));
                    if (inet_ntop(p->ai_family, &((sockaddr_in *)p->ai_addr)->sin_addr, ip_str, INET_ADDRSTRLEN)) {
                        if (protocol.length())
                            msg += protocol;
                        msg += ip_str;
                    } else {
                        DWORD e1 = WSAGetLastError();
                        msg += GetErrorMsg(e1) + "(" + ToHex(e1) + ")";
                    }
                    break;
                case AF_INET6:
                    msg += "\n";
                    switch (p->ai_protocol) {
                    case IPPROTO_IP:
                        msg += "IP/";
                        break;
                    case IPPROTO_IPV4:
                        msg += "IPv4/";
                        break;
                    case IPPROTO_IPV6:
                        msg += "IPv6/";
                        break;
                    case IPPROTO_TCP:
                        msg += "TCP/";
                        break;
                    case IPPROTO_UDP:
                        msg += "UDP/";
                        break;
                    }
                    msg += "IPv6: ";
                    char ip6_str[INET6_ADDRSTRLEN + 1];
                    ZeroMemory(ip6_str, sizeof(ip6_str));
                    if (inet_ntop(p->ai_family, &((sockaddr_in6 *)p->ai_addr)->sin6_addr, ip6_str, INET6_ADDRSTRLEN)) {
                        if (protocol.length())
                            msg += protocol + "[";
                        msg += ip6_str;
                        if (protocol.length())
                            msg += "]/";
                    } else {
                        DWORD e1 = WSAGetLastError();
                        msg += GetErrorMsg(e1) + "(" + ToHex(e1) + ")";
                    }
                    break;
                }
            }
        }
        freeaddrinfo(addr_info);
    }
    return msg;
}

void ProcessMsg(const cq::MessageEvent &e, uint64_t user_id, uint64_t discuss_id, uint64_t group_id) {
    std::string msg = e.message;
    string sms;
    try {
        std::smatch sm;
        if (msg == "帮助") {
            send_message(e.target, sms = "可用指令：\nChatLog\necho\nIP");
        } else if (lower(msg) == "chatlog") {
            send_message(e.target, sms = "ChatLog 消息记录程序\n输入“ChatLog 帮助”获取命令行使用方法。");
        } else if (lower(msg) == "chatlog 帮助") {
            send_message(
                e.target,
                sms = "ChatLog 指令：\nChatLog 记录 <n> - 发送最近n条消息记录\nChatLog 总数 - 发送已记录的消息数量");
        } else if (lower(msg) == "chatlog 总数") {
            send_message(e.target, sms = "已记录到" + std::to_string(CQGetSharedMemory().count) + "条消息。");
        } else if (lower(msg) == "echo") {
            send_message(e.target, sms = "echo 指令：\necho <消息> - 发送消息");
        } else if (lower(msg.substr(0, 5)) == "echo ") {
            EchoCommand(e, user_id, discuss_id, group_id);
        } else if (lower(msg.substr(0,2)) == "ip") {
            send_private_message(e.target.user_id.value(), sms = GetLocalAddress(msg.substr(2)));
        } else if (msg == "统计") {
            string s = iScheduleTimerID ? "开启" : "关闭";
            send_message(e.target,
                         sms = "消息统计功能状态：" + s + "\n发送“统计 开启”或“统计 关闭”可开启或关闭统计功能。");
        } else if (msg == "统计 开启") {
            StartStatScheduleTimer();
            if (iScheduleTimerID)
                send_message(e.target, sms = "已开启消息统计功能。");
            else
                send_message(e.target, sms = "无法开启消息统计功能。");
        } else if (msg == "统计 关闭") {
            StopStatScheduleTimer();
            if (iScheduleTimerID)
                send_message(e.target, sms = "无法关闭消息统计功能。");
            else
                send_message(e.target, sms = "已关闭消息统计功能。");
        } else if (std::regex_match(msg, sm, std::regex("^chatlog 记录 (\\d+)$"))) {
            if (user_id == get_login_user_id())
                send_message(e.target, sms = getRecentMessages(atoi(sm[1].str().c_str()), true));
            else
                send_message(
                    e.target,
                    sms = "你无权查看完整内容。\n" + getRecentMessages(min(5, atoi(sm[1].str().c_str())), false));
        } else if (group_id == 0 && discuss_id == 0&&friendAutoReply) {
            time_t now_t = time(NULL);
            if (friend_usage_sent.find(user_id) == friend_usage_sent.end()||now_t-friend_usage_sent[user_id]>300) {
                if (friend_usage_sent.find(user_id) == friend_usage_sent.end())
                    friend_usage_sent.insert(make_pair(user_id, now_t));
                else
                    friend_usage_sent[user_id] = now_t;
                send_message(e.target,
                             sms = "酷Q 登录账号：\n" + get_login_nickname() + "(" + to_string(get_login_user_id())
                                   + ")\n\n您可发送“帮助”获取可用指令。");
            }
        }
    } catch (const ApiError &ex) {
        ProcessException(ex, sms, user_id, discuss_id, group_id);
    }
}

std::string getFriendName(int64_t qq_id, bool contain_id) {
    auto qq_friends = get_friend_list();
    std::string r = "[NOT FOUND]";
    for (auto &f : qq_friends) {
        if (f.user_id == qq_id) {
            r = f.nickname;
            if (f.remark.length() > 0) {
                r = f.remark + "(" + f.nickname + ")";
            }
            break;
        }
    }
    if (contain_id) {
        r += "(" + std::to_string(qq_id) + ")";
    }
    return r;
}

std::string getUserName(int64_t qq_id, bool contain_id) {
    std::string r = get_stranger_info(qq_id).nickname;
    if (contain_id) {
        r += "(" + std::to_string(qq_id) + ")";
    }
    return r;
}

bool isUserMyFriend(int64_t qq_id) {
    auto qq_friends = get_friend_list();
    for (auto &f : qq_friends) {
        if (f.user_id == qq_id) {
            return true;
        }
    }
    return false;
}

std::string getGroupName(int64_t group_id, bool contain_id) {
    auto qq_groups = get_group_list();
    std::string r = "[NOT FOUND]";
    for (auto &f : qq_groups) {
        if (f.group_id == group_id) {
            r = f.group_name;
            break;
        }
    }
    if (contain_id) {
        r += "(" + std::to_string(group_id) + ")";
    }
    return r;
}

void ProcessException(const ApiError &ex, const string &msg, uint64_t user_id, uint64_t discuss_id, uint64_t group_id) {
    string sm;
    if (group_id)
        sm = "发送群消息失败：" + string(ex.what()) + "\n群：" + getGroupName(group_id, true) + "\n消息：" + msg;
    else if (discuss_id)
        sm = "发送讨论组消息失败：" + string(ex.what()) + "\n讨论组：" + to_string(discuss_id) + "\n消息：" + msg;
    else
        sm = "发送好友消息失败：" + string(ex.what()) + "\n好友：" + getFriendName(user_id, true) + "\n消息：" + msg;
    try {
        send_private_message(get_login_user_id(), sm);
    } catch (const ApiError &ex2) {
        logging::error("异常处理", "发送异常处理消息失败：" + string(ex2.what()));
    }
    logging::error("异常处理", sm);
}

void CALLBACK ScheduleCallback(UINT wTimerID, UINT nMsg, DWORD dwUser, DWORD dw1, DWORD dw2) {
    tm localtm;
    time_t localt = time(NULL);
    localtime_s(&localtm, &localt);
    if (localtm.tm_hour != last_localtm.tm_hour) {
        if (last_localtm.tm_year) {
            std::map<uint64_t, std::string> gmsg;
            for (auto &gm : group_msg_count) {
                char msg[200];
                sprintf(msg,
                        u8"本群在%d:%02d:%02d～%d:%02d:%02d期间共收到%d条消息。",
                        last_localtm.tm_hour,
                        last_localtm.tm_min,
                        last_localtm.tm_sec,
                        localtm.tm_hour,
                        localtm.tm_min,
                        localtm.tm_sec,
                        gm.second);
                gmsg.insert(std::make_pair(gm.first, msg));
            }
            group_msg_count.clear();
            for (auto &gm : gmsg) {
                Sleep(1000);
                try {
                    send_group_message(gm.first, gm.second);
                } catch (const ApiError &ex) {
                    ProcessException(ex, gm.second, 0, 0, gm.first);
                }
            }
        }
        last_localtm = localtm;
    }
}

void StopStatScheduleTimer() {
    if (iScheduleTimerID == 0) {
        logging::info("计时器", "计时器已经是关闭状态了，无需重复操作。");
        return;
    }
    logging::info("计时器", "停止统计定时器……");
    timeKillEvent(iScheduleTimerID);
    iScheduleTimerID = 0;
}

void StartStatScheduleTimer() {
    if (iScheduleTimerID) {
        logging::info("计时器", "检测到程序未正确停止统计定时器，正在停止……");
        timeKillEvent(iScheduleTimerID);
    }
    logging::info("计时器", "启动统计定时器……");
    iScheduleTimerID = timeSetEvent(60000, 60000, ScheduleCallback, NULL, TIME_PERIODIC);
}

void Init() {
    if (CQIsSharedMemoryInited()) {
        logging::info("加载", "共享内存已经加载，无需重复操作。");
        return;
    }
    if (CQInitSharedMemory()) {
        logging::info("加载", "已加载共享内存。");
    } else {
        logging::error("加载", "无法加载共享内存。");
    }
}

void Uninit() {
    StopStatScheduleTimer();
    if (!CQIsSharedMemoryInited()) {
        logging::info("释放", "共享内存已经释放，无需重复操作。");
        return;
    }
    if (CQUninitSharedMemory()) {
        logging::info("释放", "已释放共享内存。");
    } else {
        logging::error("释放", "无法释放共享内存。");
    }
}

CQ_INIT {
    on_enable([] {
        Init();
        logging::info("启用", "插件已启用");
    });
    on_disable([] {
        Uninit();
        logging::info("停用", "插件已停用");
    });
    on_coolq_start([] {
        logging::info("启动", "正在启动……");
        Init();
    });
    on_coolq_exit([] {
        logging::info("退出", "正在退出……");
        Uninit();
    });

    on_private_message([](const PrivateMessageEvent &e) {
        ProcessMsg(e, e.user_id, 0, 0);
        std::string name = "[NOT FOUND]";
        std::string remark;
        for (auto &f : get_friend_list()) {
            if (f.user_id == e.user_id) {
                name = f.nickname;
                remark = f.remark;
                break;
            }
        }
        std::string msg = e.message;
        if (remark == name) remark.clear();
        CQRecord &r = CQAddRecord(e.user_id, 0, name.c_str(), remark.c_str(), "", msg.c_str());
        if (!logMessage) return;
        std::string log = r.user_name;
        if (remark.length() > 0) log = remark + "(" + log + ")";
        if (logShowQQ) log += "(" + std::to_string(e.user_id) + ")";
        log += ":" + e.message;
        cq::logging::info("好友", log);
    });

    on_discuss_message([](const DiscussMessageEvent &e) {
        ProcessMsg(e, e.user_id, e.discuss_id, 0);
        std::string group_name = getGroupName(e.discuss_id, false);
        std::string name = "[NOT FOUND]";
        std::string remark;
        if (isUserMyFriend(e.user_id)) {
            for (auto &f : get_friend_list()) {
                if (f.user_id == e.user_id) {
                    name = f.nickname;
                    remark = f.remark;
                    break;
                }
            }
        } else {
            name = getUserName(e.user_id, false);
        }
        std::string msg = e.message;
        if (remark == name) remark.clear();
        CQRecord &r =
            CQAddRecord(e.user_id, e.discuss_id, name.c_str(), remark.c_str(), group_name.c_str(), msg.c_str());
        if (!logMessage) return;
        std::string log = group_name;
        if (logShowQQ) log += "(" + std::to_string(e.discuss_id) + ")";
        if (remark.length() > 0) name = remark + "(" + name + ")";
        if (logShowQQ) name += "(" + std::to_string(e.user_id) + ")";
        log += ":" + name;
        cq::logging::info("讨论组", log + ":" + e.message);
    });

    on_group_message([](const GroupMessageEvent &e) {
        ProcessMsg(e, e.user_id, 0, e.group_id);
        if (group_msg_count.find(e.group_id) == group_msg_count.end()) {
            group_msg_count.insert(std::make_pair(e.group_id, 0));
        }
        group_msg_count[e.group_id]++;
        std::string group_name = getGroupName(e.group_id, false);
        std::string name = "[NOT FOUND]";
        std::string remark;
        if (isUserMyFriend(e.user_id)) {
            for (auto &f : get_friend_list()) {
                if (f.user_id == e.user_id) {
                    name = f.nickname;
                    remark = f.remark;
                    break;
                }
            }
        } else {
            name = getUserName(e.user_id, false);
        }
        if (remark == name) remark.clear();
        CQRecord &r =
            CQAddRecord(e.user_id, e.group_id, name.c_str(), remark.c_str(), group_name.c_str(), e.message.c_str());
        if (!logMessage) return;
        std::string log = group_name;
        if (logShowQQ) log += "(" + std::to_string(e.group_id) + ")";
        if (remark.length() > 0) name = remark + "(" + name + ")";
        if (logShowQQ) name += "(" + std::to_string(e.user_id) + ")";
        log += ":" + name;
        cq::logging::info("群", log + ":" + e.message);
    });
}

CQ_MENU(menu_send_msg_record) {
    try {
        send_private_message(get_login_user_id(), getRecentMessages(5, true));
    } catch (const ApiError &) {
        cq::logging::warning("菜单", "发送失败");
    }
}

CQ_MENU(menu_toggle_log_show_qq) {
    logShowQQ = !logShowQQ;
    std::string msg;
    if (logShowQQ)
        msg = "已开启。";
    else
        msg = "已关闭。";
    cq::logging::info("在日志中显示QQ号", msg);
}

CQ_MENU(menu_toggle_log_message) {
    logMessage = !logMessage;
    std::string msg;
    if (logMessage)
        msg = "已开启。";
    else
        msg = "已关闭。";
    cq::logging::info("在日志中显示消息", msg);
}

CQ_MENU(menu_toggle_stat_schedule) {
    if (iScheduleTimerID)
        StopStatScheduleTimer();
    else
        StartStatScheduleTimer();
}

CQ_MENU(menu_toggle_friend_auto_reply) {
    friendAutoReply=!friendAutoReply;
    std::string msg;
    if (friendAutoReply)
        msg = "已开启。";
    else
        msg = "已关闭。";
    logging::info("好友自动回复", msg);
}
