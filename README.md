# HeicConverter

一个面向 Windows 10/11 的 HEIC 批量转 PNG/JPG 桌面程序。界面使用 Dear ImGui（Win32 + Direct3D 11），HEIC 解码使用 [libheif](https://github.com/strukturag/libheif) + libde265，PNG 写入使用 libpng，JPG 写入使用由 CMake `FetchContent` 获取的 [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo)。

## 功能

- 转换指定文件夹中的全部 `.heic` 文件（扩展名不区分大小写）
- 输出格式可在 PNG（无损、支持透明）和 JPG（质量 1–100）之间选择
- 可选递归搜索子目录
- 实时进度、成功/失败/跳过统计和逐文件日志
- 多线程并行解码与导出，默认使用检测到的逻辑处理器，也可在界面中调节线程数
- 默认中文界面，可在界面中切换 English；新任务的状态、日志及错误使用所选语言
- 可选在转换成功后删除原 HEIC
- 可选覆盖已有 PNG；默认跳过，且绝不会因此删除原文件
- 后台转换，界面不会因大文件而卡住；支持取消
- 支持中文及长路径
- Release 默认使用静态 MSVC 运行库和静态 vcpkg triplet，无需随 EXE 携带第三方 DLL

## 构建环境

- Windows 10 或更高版本（64 位）
- Visual Studio 2022 或更高版本，并安装“使用 C++ 的桌面开发”
- CMake 3.24+
- Git

在 PowerShell 中执行：

```powershell
.\build.ps1 -Configuration Release
```

脚本会优先使用环境变量 `VCPKG_ROOT` 指向的 vcpkg；如未配置，会将 vcpkg 下载到 `.deps/vcpkg`。libjpeg-turbo 由 CMake `FetchContent` 自动下载到构建目录。首次构建需要下载并编译 libheif、libde265、libpng、libjpeg-turbo 和 Dear ImGui，耗时会明显长于后续构建。

最终文件位于：

```text
dist/HeicConverter.exe
```

## GitHub Actions 自动打包

工作流位于 `.github/workflows/windows-package.yml`，会在以下情况运行：

- 推送到 `main` 或 `master`
- 创建或更新 Pull Request
- 在 Actions 页面手动触发
- 推送名称符合 `v*` 的 Git 标签

工作流会执行 Windows x64 静态 Release 构建、PNG/JPG 端到端测试，然后生成 `HeicConverter-windows-x64.zip` 并上传为保留 14 天的 Actions Artifact。推送版本标签时，ZIP 还会自动附加到对应的 GitHub Release。例如：

```powershell
git tag v1.0.0
git push origin v1.0.0
```

也可以手工配置 CMake：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release --parallel
```

## 转换和删除规则

目标图片会写入 HEIC 所在目录并使用相同的基础文件名。例如 `照片.heic` 会生成 `照片.png` 或 `照片.jpg`。程序先写入同目录的临时文件，完整写入成功后再提交为最终图片。只有目标图片成功生成后，才会尝试删除原 HEIC；失败或跳过的文件不会被删除。JPG 不支持透明通道，透明像素会合成到白色背景。

每个工作线程拥有独立的解码和编码上下文，可同时处理不同图片。较高的线程数会提高 CPU 和内存占用；处理超高分辨率照片时，如果内存压力较大，可以在界面中降低并行线程数。

## 静态发布与许可证

此配置的发布产物在技术上是单 EXE。libheif 和 libde265 使用 LGPL 许可证；若向第三方分发静态链接的 EXE，发布者仍需履行 LGPL 对许可证文本、修改源码以及重新链接能力等方面的要求。内部自用不改变程序的构建方式；公开分发前请根据实际发布方式审查并附带 `THIRD_PARTY_NOTICES.md` 及相应材料。若希望简化 LGPL 合规，可改用动态 triplet 并随程序分发 DLL。

HeicConverter 本身采用 GNU General Public License Version 3（`GPL-3.0-only`）授权，完整条款见 [LICENSE](LICENSE)。第三方组件继续适用各自的许可证，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
