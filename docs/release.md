# 发布

> 本项目通过 USB 刷写完整固件，**没有 OTA**（分区表只有 `app0`）。
> 发布物是源码 + 可从 `0x0` 刷写的合并镜像。

---

## 1. 版本号

采用 `vMAJOR.MINOR.PATCH`：

- `MAJOR`：不兼容的硬件/BSP/分区变更
- `MINOR`：新增能力、文档体系、工具
- `PATCH`：修复、文字、示例

当前基线：`v0.2.0`（AI 开发就绪改造）。

---

## 2. 发布前检查

- [ ] `git status` 干净，目标分支已合并
- [ ] `pio run -e guachat_dev` 成功
- [ ] `python tools/validate.py --static` 通过
- [ ] `python tools/validate.py --host` 通过
- [ ] 真机验证：合并镜像可刷写、可启动、串口无重启循环
- [ ] `python tools/merge_firmware.py --zip` 生成镜像、SHA256、恢复包
- [ ] `THIRD_PARTY_NOTICES.md` 与 `third_party/licenses/` 未过期
- [ ] 确认没有混入应用业务代码或第二个 `setup()`
- [ ] 没有把 `dist/`、`.pio/`、密钥、个人数据提交进仓库
- [ ] 文档中的链接与资源占用数字已更新

---

## 3. 产物

```text
build/guachat-dev-full.bin             # 0x0 合并镜像
build/guachat-dev-full.bin.sha256      # 校验和
build/guachat-dev-full-restore.zip     # 镜像 + 校验和 + 刷写说明
```

命名规则：

- 开发基线：`guachat-dev-full.bin`
- 自定义环境：`<env>-full.bin`

---

## 4. 打标签

```bash
git tag -a v0.2.0 -m "AI-ready development baseline"
git push origin v0.2.0
```

标签说明写清：

- 基线环境与资源占用
- 已验证的硬件项
- 已知未验证项
- 合并固件的 SHA256

---

## 5. 发布说明模板

```markdown
## v0.2.0 — AI 开发就绪

### 新增
- `guachat_dev` 最小开发环境 + BSP façades
- 硬件自检界面（电源/SD/传感器/LoRa/GPS/音频）
- AGENTS.md / docs / skills / tools
- `main` 只保留 BSP 与开发基线

### 验证
- Build: ...
- Host tests: ...
- Device tests: ...
- Unverified: ...

### 产物
- `guachat-dev-full.bin` (size, sha256)

### 已知问题
- ...
```

---

## 6. 不做的发布方式

- **OTA**：分区表没有 `app1`，需要重新设计分区与回滚策略；不要在补丁版本里偷偷加。
- **Web 刷机页**：可后续加，读取本地 `full.bin`，不经过服务器。
- **设备端应用商店**：不存在运行时安装；每次都是新固件。
- **把应用业务代码带进 `main`**：不要让外部 UI 生成器输出或其他业务
  重新进入开发底座。
