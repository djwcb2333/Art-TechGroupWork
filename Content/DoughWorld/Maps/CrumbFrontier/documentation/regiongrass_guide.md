# 区域草植被

已保存到 `L_CrumbFrontier_ArtPass`，共 30,747 个 UE 原生植被实例。使用 `Grass_03` 网格及全新的 `M_RegionGrass_NoRVT` 材质，不采样 RVT。每丛带轻微随机亮度和尺度差异。

| 区域 | 植被类型 | 配色 | 实例数 |
|---|---|---|---:|
| 出生点 | FT_Grass_Spawn | 灰绿 | 431 |
| 麦谷 | FT_Grass_WheatValley | 金黄 | 10507 |
| 大烤原 | FT_Grass_CrumbFrontier | 紫色 | 12505 |
| 麦村 | FT_Grass_WheatVillage | 浅金黄 | 360 |
| 霉沼 | FT_Grass_MoldSwamp | 深绿 | 5923 |
| 酵母镇 | FT_Grass_YeastTown | 浅紫 | 1021 |

植被类型位于 `Content/Maps/CrumbFrontier/Foliage/Types`，材质位于相邻 `Materials` 文件夹。区域高度读取当前关卡：出生点0、麦谷300、大烤原450、麦村330、霉沼100、酵母镇460厘米。没有覆盖用户调整过的区域高度。

道路、桥面、水池、建筑和主要点位周围已留白。麦村与酵母镇单独铺设，不与外围区域重复堆叠。草关闭投影以控制开销，使用距离剔除；本轮为静态草，无风动或交互。

## 继续刷草

按 Shift+3 打开植被模式，勾选对应区域的 `FT_Grass_*` 类型，取消其他区域类型的勾选。使用“绘制”工具在地面拖动左键补刷，按住 Shift 拖动左键擦除。因为地面是白盒静态网格，请保持“静态网格体”表面过滤开启。颜色在相应 `MI_Grass_*` 的 GrassColor 参数中调整。

六种颜色按场景视觉区域区分，不改变原阵营数据。原始白盒关卡和导入包的 RVT 材质未覆盖；新草通过植被类型的材质重载使用无 RVT 材质。

`paint_region_grass.py` 为本轮生成脚本。再次运行会重建这六类草，覆盖对这些草实例做过的手动补刷/擦除，因此不要在手工编辑后随意重跑。详细数量与抽样位置见工程内 `Content/Maps/CrumbFrontier/Documentation/RegionGrass_Report.json`。

