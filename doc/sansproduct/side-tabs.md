# Side tabs：设计与修复记录

## 目标与交互

- 在“设置 → 外观”中选择标签页位置：顶部（默认）或左侧。
- JSON 设置为 `tabPosition: "top" | "left"`，沿用已有设置模型、枚举映射和本地化入口。
- 一个窗口创建时确定布局；保存设置后，新建窗口使用新布局，现有窗口保持布局。设置中明确提示生效时机。
- 不增加切换模式的快捷键或命令。
- 左侧模式为**停靠式**：侧栏占据独立布局列，终端内容显示在其右侧，不会被标签遮挡。
  - 无标题栏行：窗口控制按钮（最小化/最大化/关闭）以圆角背板悬浮于内容右上角（`NonClientIslandWindow` overlay 模式）；顶部保留一条隐形拖拽带（约 48 DIP，左侧 148 DIP 留给侧栏开关），拖窗、双击最大化、右键系统菜单、Win11 贴靠布局全部保留。代价：拖拽带内的鼠标按下用于拖窗而不是落光标。
  - 默认展开：侧栏初始宽 200 DIP；右边 6 DIP 分隔线可用鼠标拖动，宽度限制会保留终端最小空间，并在窗口缩小时收敛。
  - 收起/展开：左上角使用 40×40 DIP 的紧凑侧栏开关，与右侧按钮组同排；展开和收起分别显示 ClosePane/OpenPane 图标，不再使用“图标 + 标签数量”的胶囊。
  - 面板内自上而下：纵向滚动的标签列表、新建标签入口、工作区入口（加大为 44 DIP 高）。
  - 只有点击侧栏开关才主动收起；选中标签或点击终端不会收起。收起后终端恢复全宽，再展开保留用户调整的宽度。
- 标签项为圆角矩形（CornerRadius 8），选中项左侧保留圆角竖条强调。
- 继续支持选择、关闭、拖动排序、跨窗口拖放，以及焦点模式、全屏下的标签显示设置（隐藏标签 UI 时侧栏开关与侧栏一起隐藏，恢复显示时重新展开）。
- 已知让步：侧栏收起时不能把其它窗口的标签直接拖到标签条上（需先展开侧栏）；`showTabsInTitlebar: false` 时使用系统标题栏，无法悬浮窗口控制按钮。

## 已有实现与根因

实现从 `feat/side-tabs` 的工作树原型开始，并在 `feat/side-tabs-abort-fix` 上完成崩溃修复与验证。后续先尝试悬浮侧栏，现已改为停靠式可调宽布局。

原有 `VerticalTabViewResources.xaml` 将 `LeftContentColumn`、`TabColumn`、`AddButtonColumn`、`RightContentColumn` 声明成了 `RowDefinition`。WinUI 2.8.4 的 `TabView::OnApplyTemplate` 将这些模板部件取作 `ColumnDefinition`，名称与类型都属于控件契约；仅保留名称不足以保证运行安全。

启动崩溃的直接原因是窗口激活消息可在侧栏 XAML 初始化期间重入，但 `_adjustProcessPriorityThrottled` 当时尚未创建。激活回调在空指针上调用 `Run()`，最终在 `RtlAcquireSRWLockExclusive` 中触发访问冲突。修复将节流器初始化移动到 `TerminalPage::Create()` 开头，并在激活回调中保留空值防护。

依据：[WinUI 2.8.4 TabView.cpp](https://github.com/microsoft/microsoft-ui-xaml/blob/v2.8.4/dev/TabView/TabView.cpp)。还需检查其横向宽度计算、滚动按钮回调及本仓库的标题栏/窗口尺寸逻辑，避免修复初始化后出现布局或滚动问题。

## 实现设计

1. 保留 TabView 所需的列表和内容模板部件。省略可选的 `*Column` 部件：源码在这些部件缺失时跳过横向宽度计算；不能用同名 `RowDefinition` 替代。纵向排布使用不带这些名称的行定义。
2. 显式应用侧边栏列表样式，使用纵向 ItemsStackPanel 和 ScrollViewer；不要将仅支持横向滚动的按钮用于纵向列表。
3. 停靠布局（`TerminalPage.xaml` 的 `SideTabLayout`/`SideTabDock`）：
   - `SideTabDock` 是常驻的紧凑侧栏开关（焦点模式/全屏隐藏标签时隐藏），位于顶部条带左侧；左侧模式首次创建时默认展开。
   - `SideTabLayout` 的三列依次为可调宽的侧栏、6 DIP 可拖分隔线、填满剩余空间的终端；`TerminalPage::Create()` 在 left 分支把 `_tabRow` 重新挂载进侧栏。焦点模式隐藏侧栏，恢复后重新展开。
   - 窗口初始尺寸不再为侧栏加宽 200 DIP（`TerminalWindow.cpp`）。
4. 悬浮标题栏（overlay 模式，`showTabsInTitlebar` 为真时生效）：
   - 链路：`TerminalPage.TitlebarOverlayMode`（可观察属性）→ `TerminalWindow` 转发 → `AppHost::_PropertyChangedHandler` → `NonClientIslandWindow::SetTitlebarOverlayMode`。订阅早于 `TerminalPage::Create()`，无初始化时序问题。
   - `NonClientIslandWindow`：内容改为占满 `_rootGrid` 两行（Row 0 + RowSpan 2）、标题栏 `Canvas.ZIndex` 置顶且背景画刷置空；标题栏画刷单独 stash 供 `_OnPaint` 在缩放时刷背景；`GetTotalNonClientExclusiveSize` 不再计入标题栏高度。
   - `TitlebarControl::SetOverlayMode`：按钮组外包 `CaptionButtonsBackdrop` 圆角背板（平时 Opacity 0 且无外边距，经典布局零变化）；隐藏底部分隔线；`DragBar` 左边距 148 DIP 给侧栏开关让位；按钮命中测试右移 8 DIP（背板内缩）。overlay 下 `_backgroundChanged` 不再按标题栏背景切换按钮深浅主题，按钮跟随应用主题（背板为主题感知色）。
   - `InfoBarContainer` 在 overlay 模式下移入内容行并置顶覆盖，48 DIP 顶边距只移动通知本身，不再参与 Auto 行测量；终端因此真正从窗口顶边开始绘制，不会留下看似“原生标题栏”的空白条。
   - 顶部拖拽带由既有 `_dragBarWindow` 输入汇聚窗口继续覆盖，贴靠布局（HTMAXBUTTON）与手动按钮悬停/点击处理不变。
5. 保留窗口创建时的模式快照，避免设置热更新将同一窗口变成不一致的混合布局。

## 验收范围

- 默认/顶部/左侧设置的读取、保存及无效值处理。
- 左侧模式下紧凑开关与停靠侧栏默认可见；侧栏初始宽 200 DIP、鼠标拖动分隔线改变宽度，多个标签按 Y 轴排列、超出可用高度可纵向滚动；终端占满右侧剩余空间。
- 标签选择不会收起侧栏；添加、关闭后状态一致；工作区和新建标签入口可用；拖动排序不被自动收起打断。
- 焦点模式隐藏侧栏开关与侧栏，退出后恢复；全屏设置决定是否显示。
- 修改设置后现有窗口保持布局，新窗口应用设置；顶部布局不回归。

## 验证结果

- x64 Debug 全量构建成功（0 错误），含悬浮标题栏（overlay）改动。
- `SideTabsPageLayout`：2/2 通过，覆盖紧凑开关/侧栏显隐、默认展开、开关 40 DIP 命中区、200 DIP 初始列宽、分隔线调宽、收起恢复全宽、展开保留宽度、焦点模式、全屏、单标签隐藏、设置重载，以及 `TitlebarOverlayMode` 在左侧模式下置位。
- `RoundtripTabPosition`：1/1 通过，覆盖默认值、读写、复制、清除和无效值。
- `VerticalTabViewLayout`：1/1 通过，覆盖纵向排列、宽度、圆角选中轨、滚动、选择和删除。此前的 0xC000027B 来自自定义 `VerticalTabViewItemStyle` 引用了 WinUI `Generic.xaml` 的私有 `TabViewCloseButtonStyle`；现已改用同一资源字典内的 `VerticalTabViewCloseButtonStyle`，不再依赖私有资源键。
- 初始化缺口修复：WinUI 可能延迟加载侧栏中的 `TabView`，因此首次 `SelectedItem` 不一定立即触发 `SelectionChanged`。`_InitializeTab()` 会在需要时直接挂载选中内容。
- dev 包实窗验证：从系统应用入口启动后正常运行。UI Automation 测得分隔线宽 6 DIP，位于侧栏与终端之间；终端内容从分隔线右侧开始，不再被侧栏覆盖。侧栏开关通过 UI Automation 收起、展开正常。鼠标事件注入未可靠触发应用，实际手动拖动仍需人工确认。
- 部署：dev 包为松散布局注册，重启应用会读取 `CascadiaPackage\bin\x64\Debug\AppX` 中的文件。增量构建后应核对该目录与 `bin\x64\Debug\TerminalApp`、`bin\x64\Debug\WindowsTerminal` 的时间戳；即使指定 `/p:DisableFastUpToDateCheck=true`，wapproj 也可能留下旧布局。运行中的开发包会锁住 EXE 和 DLL，需先关闭相应进程才能更新布局。同一版本已注册时无需重复运行注册脚本。

## 分隔线透明与侧栏开关命中修复

- 透明缝来自原 `SideTabDivider` 的 `Background="Transparent"`：页面和 XAML island 也允许透明，桌面壁纸会从 6 DIP 的分隔列透出。现在分隔线使用 `ApplicationPageBackgroundThemeBrush`，覆盖整个命中区域。
- WinUI 2 / UWP XAML 没有内置的可调列宽 `GridSplitter`。[`Windows.UI.Xaml.Controls.Primitives.Thumb`](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.controls.primitives.thumb?view=winrt-26100) 提供原生拖动、鼠标捕获和 `DragDelta` 事件，因此分隔线改用 `Thumb`，只保留侧栏宽度限制逻辑；无需增加 Toolkit 依赖。[`SplitView`](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.controls.splitview?view=winrt-26100) 虽有 inline pane 和 `OpenPaneLength`，本身不提供鼠标拖动调整宽度。
- 左上角的侧栏开关被浮动标题栏挡住：透明画刷仍会参与 XAML 命中测试。overlay 模式改为标题栏空白区域无背景画刷，让位于页面中的开关；顶部拖窗 HWND 仍覆盖其余指定区域。
- x64 Debug 全量构建成功；`SideTabsPageLayout` 2/2 通过，直接驱动 `Thumb` 的拖动事件并验证列宽变化。开发包实窗的侧栏开关已用鼠标注入验证可连续收起、展开；截图确认分隔列不再透出壁纸。鼠标注入未触发 XAML `Thumb` 的连续拖动事件，实体鼠标拖动仍需人工复核。
