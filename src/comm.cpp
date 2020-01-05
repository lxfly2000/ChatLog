#include <iostream>
#include <regex>
#include <set>
#include <sstream>

#include "cqcppsdk.h"
#include "comm.hpp"

using namespace cq;
using namespace std;

static bool logShowQQ = false;
static bool logMessage = false;

std::string getRecentMessages(int count, bool send_privacy) {
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

std::string lower(const std::string &str) {
    std::string r = str;
    for (auto &c : r) {
        if (c >= 'A' && c <= 'Z') {
            c = c + 'a' - 'A';
        }
    }
    return r;
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

CQ_INIT {
    on_enable([] { logging::info("启用", "插件已启用"); });
    on_disable([] { logging::info("停用", "插件已停用"); });
    on_coolq_start([] {
        if (CQIsSharedMemoryInited()) {
            logging::warning("加载", "检测到程序未正确释放共享内存，正在尝试释放……");
            if (CQUninitSharedMemory()) {
                logging::info("加载", "已释放。");
            } else {
                logging::error("加载", "释放失败。");
            }
        }
        if (CQInitSharedMemory()) {
            logging::info("加载", "已加载共享内存。");
        } else {
            logging::error("加载", "无法加载共享内存。");
        }
    });
    on_coolq_exit([] {
        if (CQUninitSharedMemory()) {
            logging::info("释放", "已释放共享内存。");
        } else {
            logging::error("释放", "无法释放共享内存。");
        }
    });

    on_private_message([](const PrivateMessageEvent &e) {
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
        std::string msg = e.message;
        if (lower(msg) == "chatlog" || lower(msg) == "帮助") {
            send_message(e.target, "ChatLog 消息记录程序\n输入“ChatLog 帮助”获取命令行使用方法。");
        }
        if (lower(msg) == "chatlog 帮助") {
            send_message(e.target,
                         "ChatLog 指令：\nChatLog 记录 <n> - 发送最近n条消息记录\nChatLog 总数 - 发送已记录的消息数量");
        }
        if (lower(msg) == "chatlog 总数") {
            send_message(e.target, "已记录到" + std::to_string(CQGetSharedMemory().count) + "条消息。");
        }
        std::smatch sm;
        if (std::regex_match(msg, sm, std::regex("^chatlog 记录 (\\d+)$"))) {
            if (e.user_id == get_login_user_id())
                send_message(e.target, getRecentMessages(atoi(sm[1].str().c_str()), true));
            else
                send_message(e.target,
                             "你无权查看完整内容。\n" + getRecentMessages(min(5, atoi(sm[1].str().c_str())), false));
        }

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
                CQAddRecord(e.user_id, e.group_id, name.c_str(), remark.c_str(), group_name.c_str(), msg.c_str());
        if (!logMessage) return;
        std::string log = group_name;
        if (logShowQQ) log += "(" + std::to_string(e.group_id) + ")";
        if (remark.length() > 0) name = remark + "(" + name + ")";
        if (logShowQQ) name += "(" + std::to_string(e.user_id) + ")";
        log += ":" + name;
        cq::logging::info("群", log + ":" + e.message);
    });

    on_group_upload([](const GroupUploadEvent &e) { // 可以使用 auto 自动推断类型
        stringstream ss;
        ss << "您上传了一个文件, 文件名: " << e.file.name << ", 大小(字节): " << e.file.size;
        try {
            send_message(e.target, ss.str());
        } catch (ApiError &) {
        }
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
