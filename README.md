## 测试

当你提交一个根目录包含 `CMakeLists.txt` 文件的仓库时, 测试脚本/评测平台会使用如下命令编译你的编译器:

```sh
cmake -S "repo目录" -B "build目录" -DLIB_DIR="libkoopa目录" -DINC_DIR="libkoopa头文件目录"
cmake --build "build目录" -j `nproc`
```

你的 `CMakeLists.txt` 必须将可执行文件直接输出到所指定的 build 目录的根目录, 且将其命名为 `compiler`.

如需链接 `libkoopa`, 你的 `CMakeLists.txt` 应当处理 `LIB_DIR` 和 `INC_DIR`.

模板中的 `CMakeLists.txt` 已经处理了上述内容, 你无需额外关心.
