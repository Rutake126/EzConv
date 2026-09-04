import os
import sys
import shutil
import zipfile
import subprocess
from pathlib import Path

def main():
    root = Path(__file__).resolve().parent
    release_bin = root / "build" / "_deps" / "openjpeg-build" / "bin" / "Release"
    
    print("[1/4] 检查/构建 Release 二进制文件...")
    build_cmd = ["cmake", "--build", "build", "--config", "Release", "--target", "EzConv", "jp2convert"]
    ret = subprocess.run(build_cmd, cwd=root)
    if ret.returncode != 0:
        print("错误：构建失败！")
        sys.exit(1)

    ezconv_exe = release_bin / "EzConv.exe"
    jp2convert_exe = release_bin / "jp2convert.exe"
    if not ezconv_exe.exists() or not jp2convert_exe.exists():
        print(f"错误：未找到生成的可执行文件：{ezconv_exe}")
        sys.exit(1)

    print("[2/4] 准备发布包目录...")
    dist_dir = root / "dist"
    package_dir = dist_dir / "EzConv_v1.1.0"
    if package_dir.exists():
        shutil.rmtree(package_dir)
    package_dir.mkdir(parents=True, exist_ok=True)

    print("[3/4] 复制程序文件与依赖...")
    # 复制主可执行文件
    shutil.copy2(ezconv_exe, package_dir / "EzConv.exe")
    shutil.copy2(jp2convert_exe, package_dir / "jp2convert.exe")
    
    # 复制说明文档
    if (root / "README.md").exists():
        shutil.copy2(root / "README.md", package_dir / "README.md")
    
    docs_dir = package_dir / "docs"
    docs_dir.mkdir(exist_ok=True)
    if (root / "docs" / "DJVU_TO_PDF_PRD.md").exists():
        shutil.copy2(root / "docs" / "DJVU_TO_PDF_PRD.md", docs_dir / "DJVU_TO_PDF_PRD.md")

    # 复制 djvulibre 依赖工具包
    djvu_src = root / "third_party" / "djvulibre" / "bin"
    djvu_dst = package_dir / "djvulibre" / "bin"
    if djvu_src.exists():
        shutil.copytree(djvu_src, djvu_dst)

    # 为了让用户把单个 Exe 拖到任意文件夹都能顺畅运行，也在同级目录备一份
    for item in djvu_src.iterdir():
        if item.is_file():
            shutil.copy2(item, package_dir / item.name)

    print("[4/4] 正在打包生成 Zip 压缩归档...")
    zip_path = dist_dir / "EzConv_v1.1.0_Windows_x64.zip"
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zipf:
        for file_path in package_dir.rglob('*'):
            if file_path.is_file():
                arcname = file_path.relative_to(dist_dir)
                zipf.write(file_path, arcname)

    print("\n=======================================================")
    print(" 打包完成！发布产物位于：")
    print(f" 目录发布包: {package_dir}")
    print(f" 压缩归档包: {zip_path} (大小: {zip_path.stat().st_size / 1024 / 1024:.2f} MB)")
    print("=======================================================")

if __name__ == '__main__':
    main()
