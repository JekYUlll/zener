#ifndef ZENER_FILE_H
#define ZENER_FILE_H

// TODO 设计一个文件系统

#include "common.h"

#include <string>

namespace zener {
namespace fs {

class File {
  public:
    enum FileType {
        Text,
        Image,
        Video,
    };

  public:
    explicit File(const std::string& path);
    ~File();

    const char* Name();

    _ZENER_SHORT_FUNC const char* GetPath() const { return _path.c_str(); }

  private:
    std::string _path;
};

} // namespace fs
} // namespace zener

#endif // !ZENER_FILE_H
