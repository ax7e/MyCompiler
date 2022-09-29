## 测试

使用如下命令编译:

```sh
cmake -S "repo目录" -B "build目录" -DLIB_DIR="libkoopa目录" -DINC_DIR="libkoopa头文件目录"
cmake --build "build目录" -j `nproc`
```

