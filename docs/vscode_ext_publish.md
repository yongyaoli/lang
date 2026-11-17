# VS Code 扩展发布指南 (Visual Studio Marketplace 与 Open VSX)

本文档说明如何使用项目提供的自动化脚本，将 L 语言官方 VS Code 扩展 (`l-language-support`) 发布至全球两大扩展生态市场：
1. **Visual Studio Marketplace**（微软官方 VS Code / VS Code Insiders 扩展商店）
2. **Open VSX Registry**（开源无版权扩展市场，服务于 VSCodium, Cursor, Gitpod, Eclipse Theia 等开源 IDE）

发布脚本路径：[`scripts/publish_vscode_ext.ps1`](../scripts/publish_vscode_ext.ps1)（同时提供 CMD 启动器 [`scripts/publish_vscode_ext.bat`](../scripts/publish_vscode_ext.bat)）。

---

## 快速发布（推荐）

### 方式 1：交互式一键运行（小白友好）
在 PowerShell 中直接运行：
```powershell
.\scripts\publish_vscode_ext.ps1
```
脚本会自动检测 `dist/` 目录下的 `.vsix` 文件，若未生成则会自动打包。随后脚本会交互式提示您输入两大平台的 Token，输入后即可自动发布（直接回车可跳过某个平台）。

### 方式 2：命令行静默一键发布
```powershell
.\scripts\publish_vscode_ext.ps1 -MarketplaceToken "<你的VSCE_PAT>" -OpenVSXToken "<你的OVSX_TOKEN>"
```

### 方式 3：持久化环境变量（省去每次敲 Token）
在当前终端或系统环境变量中配置：
```powershell
$env:VSCE_PAT = "你的微软PAT"
$env:OVSX_PAT = "你的OpenVSX_TOKEN"
.\scripts\publish_vscode_ext.ps1
```

---

## 第一部分：Visual Studio Marketplace (微软官方) Token 获取

### 步骤 1：创建发布者 (Publisher)
1. 访问微软扩展管理后台：[https://marketplace.visualstudio.com/manage](https://marketplace.visualstudio.com/manage)
2. 使用微软账号登录。
3. 点击 **Create publisher** 创建发布者。
   > ⚠️ **关键约定**：发布者 ID (`Publisher ID`) 必须与 `editors/vscode/package.json` 中的 `"publisher"` 字段完全一致！
   > 当前项目的 publisher 为：`yongyaoli`

### 步骤 2：获取 Personal Access Token (PAT)
1. 登录 Azure DevOps：[https://dev.azure.com/](https://dev.azure.com/)
2. 点击右上角 **User settings**（齿轮旁边的人形图标）-> 点击 **Personal access tokens**。
3. 点击 **+ New Token**：
   - **Name**：输入名称（例如 `vsce-publisher`）
   - **Organization**：⚠️ **【关键避坑】** 必须下拉选择 **`All accessible organizations`**（不能选单一组织，否则 vsce 认证必定报 401 失败）
   - **Expiration**：选择过期时间（推荐 90 天或 1 年）
   - **Scopes**：点击最下方 **Show all scopes**：
     - 滚动找到 **Marketplace** 区域；
     - 勾选 **Acquire** 和 **Manage** 两项（或者直接选择最顶部的 `Full access`）
4. 点击 **Create** 生成 Token，**立即复制保存**该 52 位 Token（页面刷新后将无法再次查看）。

---

## 第二部分：Open VSX Registry (开源生态) Token 获取

Open VSX 由 Eclipse 基金会托管，是开源编辑器（如 VSCodium、Cursor、Gitpod）的默认扩展源。

### 步骤 1：创建与认领命名空间 (Namespace)
1. 访问 Open VSX 官网：[https://open-vsx.org/](https://open-vsx.org/)
2. 点击右上角 **Log in**，使用 GitHub 账号授权登录。
3. 登录后进入 [Namespaces 页面](https://open-vsx.org/user-settings/namespaces) 或在首页申请创建命名空间：
   > ⚠️ **关键约定**：Namespace 名称必须与 `package.json` 中的 `"publisher"` 一致（即 `yongyaoli`）。
4. 提交认领（Claim）后即刻拥有该命名空间的发布权限。

### 步骤 2：获取 Access Token
1. 点击右上角个人头像 -> 进入 **Settings**。
2. 切换到左侧导航的 **Access Tokens**。
3. 在 **Create a new token** 区域输入描述（如 `ovsx-publish-token`），点击 **Create**。
4. 复制生成的 Access Token。

---

## 常见问题排查 (FAQ)

| 报错现象 | 根本原因 | 解决办法 |
| :--- | :--- | :--- |
| `ERROR: 401 Unauthorized` | Azure DevOps PAT 的 Organization 选择了特定组织，或者权限未勾选 Manage | 重新在 Azure DevOps 创建 PAT，Organization 必须选择 **All accessible organizations**，Scopes 必须勾选 **Marketplace -> Manage** |
| `Publisher not found` | 商店后台尚未创建对应的发布者 | 前往 [https://marketplace.visualstudio.com/manage](https://marketplace.visualstudio.com/manage) 创建与 `package.json` 一致的 Publisher ID |
| `Version 0.3.0 already exists` | 该版本号之前已发布过 | 递增 `editors/vscode/package.json` 中的 `"version"`（如 `0.3.1`），然后重新运行发布脚本 |
| 发布成功但在商店搜不到 | 平台安全扫描与 CDN 同步延迟 | 属于正常现象，微软与 Open VSX 均需要 2~5 分钟进行自动化安全扫描与索引同步，稍等片刻即可搜索到 |
