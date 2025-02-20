#ifndef ZENER_UTILS_RELEASE_HPP
#define ZENER_UTILS_RELEASE_HPP

namespace zws {

template <typename Interface>
inline void SafeRelease(Interface*& pInterfaceToRelease) {
    if (pInterfaceToRelease) {
        delete pInterfaceToRelease;
        pInterfaceToRelease = nullptr;
    }
}

} // namespace zws

#endif // !ZENER_UTILS_RELEASE_HPP
