# 面团世界：当前资产使用决定

更新于2026-09-13。**Fantastic Village 已经用于原DoughWorldDepth地图，本轮不需要继续采购资产。用户明确不喜欢Mushroom资产包，已将它排除，不再推荐或安排下载。**

Fantastic本轮实际取用30种网格、放置60个实例，主要补充基地的制作台与仓储、面包原料、生活家具、小车、废墟杂物，以及木桥外观。现有SoStylized地形岩石与分区草、面团建筑轮廓、独立屋顶继续保留；没有搬入一整座中世纪村庄。

- [本轮实际环境资产清单](VillagePass/EnvironmentAssetList.md)：30种已用网格、实例数、用途、碰撞和材质依赖。
- [程序接入说明](VillagePass/Integration.md)：原50个锚点、隐藏代理、桥面碰撞、屋顶渐隐及迁移依赖。
- [实际构建记录](VillagePass/BuildReport.json)：源/目标路径、60个Actor的实际数据及56个旧代理处理结果。
- [本轮选型草案](VillagePass/AssetPlan.md)：保留候选思路；实际采用结果以上三项为准。

最终摆放已根据真实网格射线修正货架袋篮和屋内面包贴合，并调整底层箱净空；补给车增加一袋实际贴合车床的货物。资产数量以最新BuildReport为准；终态1171Actor、1110条Manifest记录，实际运行证据汇总在[本轮交付说明](VillagePass/README.md)。

本轮所有新增网格副本位于 `/Game/Maps/DoughWorldDepth/Village/Meshes`，仍引用 `/Game/Fantastic_Village_Pack` 内原材质、父材质与贴图。程序或美术移交时应带齐这些依赖，不能只提取Meshes文件夹。

本页不沿用此前商品价格，也不对当前商城售价作判断。旧研究已原样备份为 [2026-09-10历史建议](VillagePass/AssetRecommendations_Previous_20260910.md)，其中购买顺序、价格与Mushroom建议均属于历史记录，已被当前决定取代。洞穴包也不在本轮采购安排中。

现阶段先完成现有资产的场景适配和实际运行验证。需要专门设计的面团切面、孔洞、祭坛及角色造型继续使用现有占位或项目定制方向；本轮不新增采购要求，不将环境摆件描述为已完成的游戏机制。终态编辑器、实例/隐藏代理与保存检查已通过；8个固定相机站位和6段基地往返行走有实际PIE记录，具体范围见[本轮交付说明](VillagePass/README.md)。
