#include "Db.h"

#include <cstdlib>
#include <stdexcept>

namespace db {

std::string connectionStringFromEnv()
{
    const char* url = std::getenv("DATABASE_URL");
    if (url == nullptr || std::string(url).empty()) {
        throw std::runtime_error(
            "La variable d'environnement DATABASE_URL n'est pas definie.\n"
            "Exemple (PowerShell) : $env:DATABASE_URL=\"postgresql://user:motdepasse@hote/base?sslmode=require\"\n"
            "Exemple (bash)       : export DATABASE_URL=\"postgresql://user:motdepasse@hote/base?sslmode=require\"");
    }
    return url;
}

} // namespace db
