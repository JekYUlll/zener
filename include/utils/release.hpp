#ifndef ZWS_UTILS_RELEASE_HPP
#define ZWS_UTILS_RELEASE_HPP

namespace zws {

template <typename Interface>
inline void SafeRelease(Interface*& pInterfaceToRelease) {
    if (pInterfaceToRelease) {
        delete pInterfaceToRelease;
        pInterfaceToRelease = nullptr;
    }
}

} // namespace zws

#endif // !ZWS_UTILS_RELEASE_HPP