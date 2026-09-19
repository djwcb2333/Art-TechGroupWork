# 源文件与证据归档

`archive_delivery.py` 默认只生成工作区的 `ArchiveDryRun.json`。加入 `--apply` 后，才把文件复制到 `E:/Unreal Project/GDATtest/Content/Maps/DoughWorldDepth/Source` 和 `Documentation`。运行前应保存地图并正常关闭 UE；脚本发现仍有 UnrealEditor 进程时拒绝复制。

## 运行

在本交付目录内使用 Python 运行：

```powershell
& 'C:/Users/67582/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' -X utf8 .\archive_delivery.py
& 'C:/Users/67582/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' -X utf8 .\archive_delivery.py --apply
```

需要只收录本轮重开验证的运行报告和截图时，在两次命令中都加 `--verify-after '2026-09-10T14:00:00Z'`，实际时间应使用本轮验证开始时间。该参数只限制运行证据与截图；原件、设计源文件和未变化的制作数据仍会归档。早于时间的文件会明确列为跳过，不会被标为验证失败或通过。

新增最终证据可以写成一个工作区内的 JSON 字符串数组，例如 `["FinalPersistenceVerification.json"]`，再通过 `--evidence-list '.\FinalEvidenceList.json'` 指定；必须是本交付目录内的相对路径。最终编译日志可用 `--build-log '.\FinalBuild.log'` 附带，脚本保留原文，不推断编译结果。

## 归档结构和来源

| 目录 | 内容 |
|---|---|
| `Source` | 复用 SourceArchiveDraft 的原始 JSON、PNG、R16、高度图、Geometry/OBJ/MTL 映射。每次重新算 SHA256，旧草稿哈希只用于记录变化。 |
| `Source/NativeCodeReference` | 直接读取真实工程 `Source/GDATtest` 的所有 H/CPP/Build.cs，包括角色与控制器对接。这些副本不从 Content 编译。 |
| `Source/AuthoringProject` | 按工作区原相对路径保留交付包、外部地形生成数据、几何生成数据、明确选定的制作与验收脚本和场景清单。 |
| `Documentation` | 场景说明、验收结果、程序对接、环境资产清单、来源差异及资产选型文档。 |
| `Documentation/Evidence` | 显式列出的实际运行报告；原文保留，不因复制成功而更改其测试状态。 |
| `Documentation/Captures` | `01_` 至 `99_` 命名的真实运行截图，原型阶段 `Prototype_` 图片不收录。 |
| `Documentation/ArchiveReport.json` | 每个文件的来源、目标、SHA256、时间戳、复制/相同跳过状态及原生源码一致性检查。 |

原生源码与工作区 `CameraCode` 中同名源码如有任何字节差异，整个复制预检会拒绝执行并列出差异。应先由制作人员确认版本、同步真正需要的修改并完成编译，再运行归档；脚本不自动以工作区旧版本覆盖工程。编译是否通过仍以最终编译日志与 `TestResults.md` 为准。

`GhostTransform.json`、`HoleMaskDiagnosis.json`、导航二分诊断、旧原型结果、未完成的全部环境静态化结果和 `Backups` 不进入最终运行证据。它们留在工作区用于追溯。`Source/AuthoringProject/Planning/Geometry` 中的生成器检查属于制作过程记录，不能替代 UE 运行验收。

## 安全和复用

脚本只允许从本交付目录与真实工程 `Source/GDATtest` 读取，所有目标逐项 `resolve()` 验证在上述 Content 根下的 Source 或 Documentation 内；不删除文件，不改 UAsset/UMap，不启动 UE，不触发自动导入。字节相同的目标跳过，发生复制后重新算哈希。复制报错时报告保留已经处理的项目，不伪称整批完成。

归档的脚本是制作参考。不要在 Content 中直接执行重建脚本，也不要把整批源文件交给 UE 自动导入；如果需要重建，先复制 `Source/AuthoringProject` 到独立工作目录，阅读 `Integration.md` 并确认各脚本的地图保护断言、输入路径和当前版本。`build_all.py` 等重建脚本会重新生成场景，不能用于普通打开地图。
