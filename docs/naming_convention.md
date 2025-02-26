# Zener服务器可执行文件命名约定

## 命名方式

在Zener服务器项目中，可执行文件按以下约定命名：

1. **主可执行文件**：
   - `bin/Zener` - 默认的服务器可执行文件（不带实现后缀）

2. **特定实现的可执行文件**（使用连字符分隔）：
   - `bin/Zener-map` - 使用MAP定时器实现的服务器
   - `bin/Zener-heap` - 使用HEAP定时器实现的服务器

## 向后兼容性

为了保持向后兼容性，我们创建了符号链接，将旧的下划线命名方式链接到新的标准命名方式：

- `bin/Zener_map` -> `bin/Zener-map`
- `bin/Zener_heap` -> `bin/Zener-heap`
- `bin/Zener_default` -> `bin/Zener`

## 脚本

以下脚本与命名约定相关：

1. **build_all.sh** - 构建所有版本的服务器，包括MAP和HEAP实现
2. **create_symlinks.sh** - 创建符号链接以保持向后兼容性
3. **run_server.sh** - 运行指定版本的服务器
4. **benchmark.sh** - 对服务器执行基准测试

## 如何使用

无论您使用哪种命名约定，两种格式都可以运行：

```bash
# 以下命令是等效的
./bin/Zener-map
./bin/Zener_map

# 以下命令是等效的
./bin/Zener-heap
./bin/Zener_heap
```

## 更新历史

- 2025-02-27: 将命名标准从下划线（`_`）更改为连字符（`-`）
- 添加符号链接以保持向后兼容性 