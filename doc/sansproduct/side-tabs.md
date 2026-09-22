# Side tabs：设计与修复记录

## 目标与交互

- 在“设置 → 外观”中选择标签页位置：顶部（默认）或左侧。
- JSON 设置为 `tabPosition: "top" | "left"`，沿用已有设置模型、枚举映射和本地化入口。
- 一个窗口创建时确定布局；保存设置后，新建窗口使用新布局，现有窗口保持布局。设置中明确提示生效时机。
- 不增加切换模式的快捷键或命令。
- 左侧栏固定宽度为 200 DIP；工作区入口在上方，标签列表在中间纵向滚动，新建标签入口在下方。终端内容占用剩余空间。
- 继续支持选择、关闭、拖动排序、跨窗口拖放，以及焦点模式、全屏下的标签显示设置。

## 已有实现与根因

实现从 `feat/side-tabs` 的工作树原型开始，并在 `feat/side-tabs-abort-fix` 上完成崩溃修复与验证。

原有 `VerticalTabViewResources.xaml` 将 `LeftContentColumn`、`TabColumn`、`AddButtonColumn`、`RightContentColumn` 声明成了 `RowDefinition`。WinUI 2.8.4 的 `TabView::OnApplyTemplate` 将这些模板部件取作 `ColumnDefinition`，名称与类型都属于控件契约；仅保留名称不足以保证运行安全。

启动崩溃的直接原因是窗口激活消息可在侧栏 XAML 初始化期间重入，但 `_adjustProcessPriorityThrottled` 当时尚未创建。激活回调在空指针上调用 `Run()`，最终在 `RtlAcquireSRWLockExclusive` 中触发访问冲突。修复将节流器初始化移动到 `TerminalPage::Create()` 开头，并在激活回调中保留空值防护。

依据：[WinUI 2.8.4 TabView.cpp](https://github.com/microsoft/microsoft-ui-xaml/blob/v2.8.4/dev/TabView/TabView.cpp)。还需检查其横向宽度计算、滚动按钮回调及本仓库的标题栏/窗口尺寸逻辑，避免修复初始化后出现布局或滚动问题。

## 实现设计

1. 保留 TabView 所需的列表和内容模板部件。省略可选的 `*Column` 部件：源码在这些部件缺失时跳过横向宽度计算；不能用同名 `RowDefinition` 替代。纵向排布使用不带这些名称的行定义。
2. 显式应用侧边栏列表样式，使用纵向 ItemsStackPanel 和 ScrollViewer；不要将仅支持横向滚动的按钮用于纵向列表。
3. 检查固定侧栏宽度、标签内容裁剪、窗口初始尺寸和可见性计算；已有顶部模式沿用原有模板。
4. 保留窗口创建时的模式快照，避免设置热更新将同一窗口变成不一致的混合布局。

## 执行计划

- [x] 检查当前改动与构建环境，创建 feat 修复分支。
- [x] 记录设置交互、布局设计和修复计划。
- [x] 核对 WinUI 模板契约，增加能实际应用模板的回归测试并复现失败。
- [x] 修复模板、滚动以及必要的窗口布局问题。
- [x] 修复初始化重入崩溃和设置重载后的侧栏可见性。
- [x] 构建 x64 Debug，运行设置模型及 XAML/TerminalPage 回归测试。
- [x] 部署开发包并在 `tabPosition: "left"` 下完成启动验证。

## 验收范围

- 默认/顶部/左侧设置的读取、保存及无效值处理。
- 左侧模式实际创建并布局控件时不崩溃；多个标签按 Y 轴排列，超出可用高度时可以纵向滚动。
- 标签选择、添加、关闭后状态一致；工作区和新建标签入口可用。
- 焦点模式隐藏侧栏，退出后恢复；全屏设置决定是否显示侧栏。
- 修改设置后现有窗口保持布局，新窗口应用设置；顶部布局不回归。

## 验证结果

- x64 Debug 测试宿主和 `CascadiaPackage` 构建成功。
- `VerticalTabViewLayout`：1/1 通过，覆盖纵向排列、宽度、滚动、选择和删除。
- `SideTabsPageLayout`：2/2 通过，覆盖标题栏设置、焦点模式、全屏、单标签隐藏及设置重载。
- `RoundtripTabPosition`：1/1 通过，覆盖默认值、读写、复制、清除和无效值。
- 开发包部署成功；保留 `tabPosition: "left"` 启动后进程持续运行，未产生新的崩溃转储。
