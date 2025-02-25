#ifndef ZENER_UTILS_RELEASE_HPP
#define ZENER_UTILS_RELEASE_HPP

namespace zener {

template <typename Interface>
void SafeRelease(Interface*& pInterfaceToRelease) {
    if (pInterfaceToRelease) {
        delete pInterfaceToRelease;
        pInterfaceToRelease = nullptr;
    }
}

} // namespace zener

#endif // !ZENER_UTILS_RELEASE_HPP
