/**
 * @file bugfixes.h
 * @brief 关键Bug修复文档
 *
 * 本文件记录了为服务器修复的关键Bug，包括内存安全问题和逻辑错误。
 */

#ifndef ZENER_OPTIMIZATIONS_BUGFIXES_H
#define ZENER_OPTIMIZATIONS_BUGFIXES_H

namespace zener {
namespace optimizations {

/**
 * @brief 修复的关键Bug汇总
 *
 * 1. Response::addContent 中的文件映射问题
 *    - 原错误：mmap返回值被错误地解释为int指针，然后解引用检查是否为-1
 *    - 修正：使用MAP_FAILED直接比较mmap返回值，添加更详细的错误处理和日志
 *    - 影响：修复了在处理文件请求时可能导致的段错误
 *
 * 2. Buffer::ReadFd 中的边界检查问题
 *    - 原错误：extraBuff太大(65535字节)可能导致栈溢出，且没有足够的边界检查
 *    - 修正：减小extraBuff大小至16KB，添加缓冲区空检查和异常处理
 *    - 影响：提高了高并发下读取操作的稳定性
 *
 * 3. Conn::write 方法中的ET模式处理
 *    - 原错误：ET模式下可能一次写不完所有数据，但没有循环写逻辑
 *    - 修正：添加循环写入逻辑，适当处理EAGAIN错误
 *    - 影响：修复ET模式下可能的数据发送不完整问题
 *
 * 4. Conn::process 方法中的iov缓冲区设置
 *    - 原错误：_iovCnt可能未正确初始化，导致ToWriteBytes()返回垃圾值
 *    - 修正：确保_iovCnt正确初始化，添加对iov[1]的正确初始化
 *    - 影响：防止在发送响应时访问未初始化的内存
 *
 * 5. 高并发下mmap资源管理问题
 *    - 原错误：大量并发请求同一文件时，每个连接都会独立调用mmap映射文件
 *    - 修正：实现文件映射缓存，对相同路径的文件复用内存映射，避免过多的系统调用
 *    - 影响：减少内存消耗和系统调用开销，防止在高并发下耗尽虚拟内存空间
 */

} // namespace optimizations
} // namespace zener

#endif // ZENER_OPTIMIZATIONS_BUGFIXES_H