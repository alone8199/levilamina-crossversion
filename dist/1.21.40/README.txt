CrossVersion 单插件构建 - BDS 1.21.40.x
========================================

目标 LeviLamina : 21.40.0（若 xmake-repo 包名不同，用 --ll_version 覆盖）
服务端协议号   : 748
产物           : CrossVersion-levilamina-1.21.40-windows-x86_64.zip

路由表
  登记的全部客户端协议 : 26 个
  已实现 codec 的协议   : 898, 944, 975, 1001
  待补 codec 的协议     : 22 个（当前按 passthrough / strict 处理）

本版本状态
  网络 hook 符号 : 待补齐（网络接管关闭，插件仍可加载与配置）
  codec 可编译   : 否，需补协议定义

安装
  解压后把 CrossVersion/ 放入 BDS 的 plugins/ 目录，
  codec_v*.dll 放在 plugins/CrossVersion/codecs/ 目录。

说明
  本构建与其他 BDS 版本的构建互不通用：hook 的是 BDS 内部符号，逐版本变化。
  配置见 config.jsonc；修改后重启服务器生效（Plugin::reload() 接口已预留）。
