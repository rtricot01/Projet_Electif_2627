#ifndef USER_H_INCLUDED
#define USER_H_INCLUDED
using namespace std;

#include <string>
#include <vector>

class User {
public:
    int id = 0;
    string username;
    string password;
    string nom;
    string prenom;

    User() = default;
    User(int id, string username, string password, string nom, string prenom);
};

class UserManager {
private:
    string m_connection_string;

public:
    explicit UserManager(const string& conn_str);

    bool registerUser(const string& username, const string& password);
    bool authenticate(const string& username, const string& password);
    vector<User> getAllUsers();
};

#endif // USER_H_INCLUDED
