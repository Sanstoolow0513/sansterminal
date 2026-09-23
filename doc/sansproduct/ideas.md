# 有关sansterminal的实现想法和功能预览

## bg

当前的ide or coding agent gui等等都是类似于cli server套壳gui，我认为这种主义是完全错误的，就是需要terminal，所以本产品基于windows terminal的完善基础上构建

- 维持windows terminal有关终端的实现

- 添加大量个人的idea

## feats

- workspace设计，针对不同的工作区分区域形成不同的分组，完成对应的切换方法以及好用的tab切换（不在维持当前的header bar采用side tabs方法，这样子展示内容更加核心）

- add files设计，修改terminal功能拓展到可能看到文本内容等功能，这些是全新领域，需要大量的调研设计

- 最后形成的是workspace with files (类似于ide) 同时可以很方便的切换不同的工作区实现功能