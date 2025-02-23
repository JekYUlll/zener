#ifndef ZENER_FILE_H
#define ZENER_FILE_H

#include "common.h"

#include <string>

namespace zws {
namespace fs {

class File {
  public:
    enum FileType {
        Text,
        Image,
        Video,
    };

  public:
    File(const std::string& path);
    ~File();

    const char* Name();

    _ZENER_SHORT_FUNC const char* GetPath() { return _path.c_str(); }

  private:
    std::string _path;
};

} // namespace fs
} // namespace zws

#endif // !ZENER_FILE_H
