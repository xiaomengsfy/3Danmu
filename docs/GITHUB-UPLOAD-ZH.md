# 上传 GitHub

1. 解压源码 ZIP，进入唯一顶层目录。
2. 在 GitHub 建立仓库，建议说明它基于 OakRoller/3Danmu 的独立改版；若打算走 fork 流程，在自己的 fork 检出目录中应用这份源码快照，保留原有 Git 历史。
3. 将本目录内容作为仓库根目录上传：README.md、source/、romfs/、cia/、test/、tools/、licenses/ 等；不要只上传这个 ZIP 来代替可浏览源码。
4. 保留 LICENSE、THIRD-PARTY-NOTICES.md、licenses/、来源说明；确认 README 的功能和未验证项符合实际设备结果。
5. 首次提交后可标记功能版本；若另外发布 CIA/3DSX，把它们放在 Release，并配套相应源码、第三方许可及所需重链接材料。

命令行新建仓库示例（请先把远端地址改成自己的，以下命令本次未执行）：

```bash
git init
git add .
git commit -m "Import 3Danmu Kina v1.3.8 source"
git branch -M main
git remote add origin https://github.com/YOUR-NAME/YOUR-REPO.git
git push -u origin main
```

本包不含 .git 历史、编译结果、开发环境、个人 cookie、进度或设备固件。新增 .gitignore 项保护常见本地运行数据。所有应用源码与资源和当前 v1.3.8 工作树逐文件核对；测试仅调整相对引用与临时数据落点，原开发工作树未改写。
