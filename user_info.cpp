#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

bool is_system_admin(const std::string& username, uid_t uid) {
    if (uid == 0) return true;

    FILE* fp = fopen("/etc/group", "r");
    if (!fp) return false;

    char line[1024];

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';

        char* tmp = strdup(line);
        if (!tmp) continue;

        char* saveptr = nullptr;
        char* name = strtok_r(tmp, ":", &saveptr);
        strtok_r(nullptr, ":", &saveptr);
        strtok_r(nullptr, ":", &saveptr);
        char* members = strtok_r(nullptr, ":", &saveptr);

        if (name && members &&
            (strcmp(name, "sudo") == 0 ||
             strcmp(name, "wheel") == 0 ||
             strcmp(name, "adm") == 0)) {

            if (strstr(members, username.c_str())) {
                free(tmp);
                fclose(fp);
                return true;
            }
        }

        free(tmp);
    }

    fclose(fp);
    return false;
}

std::vector<std::string> get_user_groups(const std::string& username) {
    std::vector<std::string> result;

    FILE* fp = fopen("/etc/group", "r");
    if (!fp) return result;

    char line[1024];

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';

        char* tmp = strdup(line);
        if (!tmp) continue;

        char* saveptr = nullptr;
        char* name = strtok_r(tmp, ":", &saveptr);
        strtok_r(nullptr, ":", &saveptr);
        strtok_r(nullptr, ":", &saveptr);
        char* members = strtok_r(nullptr, ":", &saveptr);

        bool is_member = false;

        if (members && strstr(members, username.c_str())) {
            is_member = true;
        }

        if (is_member && name) {
            result.push_back(name);
        }

        free(tmp);
    }

    fclose(fp);
    return result;
}

int main() {
    struct passwd* pw;

    std::cout << "=== Отчёт по пользователям системы ===\n\n";

    setpwent();

    while ((pw = getpwent()) != nullptr) {
        std::string uname = pw->pw_name;

        std::cout << "Пользователь: " << uname << "\n";
        std::cout << "  UID: " << pw->pw_uid << "\n";
        std::cout << "  Домашний каталог: " << pw->pw_dir << "\n";

        std::vector<std::string> groups = get_user_groups(uname);

        std::cout << "  Группы: ";

        if (groups.empty()) {
            std::cout << "(не состоит)";
        } else {
            for (size_t i = 0; i < groups.size(); ++i) {
                std::cout << groups[i];
                if (i + 1 < groups.size())
                    std::cout << ", ";
            }
        }

        if (is_system_admin(uname, pw->pw_uid)) {
            std::cout << "  => ADMIN";
        }

        std::cout << "\n\n";
    }

    endpwent();

    return 0;
}