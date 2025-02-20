#ifndef ZENER_FILE_H
#define ZENER_FILE_H

#include <string>

namespace zws {

class File {

    enum FileType {
        Text,
        Image,
        Video,
    };

  public:
    File(const std::string& path);
    ~File();

  private:
    std::string _path;
};

} // namespace zws

#endif // !ZENER_FILE_H
