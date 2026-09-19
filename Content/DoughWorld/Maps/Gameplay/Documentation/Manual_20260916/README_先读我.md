# Dough World 文档入口

第一次做 UI：双击 `DoughWorld_UI从零开始_图解教程.html`。包含真实截图框注、Designer 基础、贴图/字体/声音接入、开场与主菜单、翻页过渡、镜头轻摆、可挂载弹跳组件，以及材质/遮挡操作附录。

修改已有功能和查全部参数：双击 `DoughWorld_完整操作与修改手册.html`。页面可离线阅读、目录跳转、搜索和打印。Markdown 文件是可编辑原稿。

`assets` 必须和 HTML/Markdown 一起保留。其内容只包含交付图片、标注 SVG 和放大指引，没有制作脚本或缓存。标注 SVG 内嵌真实原图，原始截图也可从放大指引中打开。

`current_project_snapshot.json`、`editable_schema.json`、`KeyBindingsRuntimeQA.json` 保留各自采集日期，是核对记录，不是导入工具。旧快照不代表新增前端资源的实时状态；历史按键测试也不替代这次菜单、镜头和弹跳验证。

`Verification` 保存本轮前端编译日志、20 项开场/组件检查、34 项页面/相机检查及 15 项原玩法地图/原 WBP 页面检查。PIE 已完成，未打包独立 EXE，未做 FPS / GPU 性能基准。Logo 和新增音效槽是待替换/待填入的占位。

修改默认键位：打开 `Content/DoughWorld/Maps/Gameplay/Blueprints/BP_DWPlayerController` → 类默认值 → 搜索 Key → 修改 → 编译保存 → 重新开始游戏。其他入口请按教程导航查找。
