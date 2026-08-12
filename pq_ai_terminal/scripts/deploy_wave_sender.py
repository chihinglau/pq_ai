#!/usr/bin/env python3
"""
部署脚本 - 将wave_sender_arm部署到T536和RK3576
使用paramiko进行SSH连接
"""

import os
import sys
import subprocess
import time

# 配置信息
CONFIG = {
    # 交叉编译服务器
    'cross_compile': {
        'host': '192.168.72.128',
        'port': 22,
        'user': 'liuzhixing',
        'password': '123456',
        'project_dir': '/home/liuzhixing/pq_ai/pq_ai_terminal',
        'cross_compiler': '/opt/scm/gcc-linaro-5.3.1-2016.05-x86_64_arm-linux-gnueabihf/bin',
    },
    # T536
    't536': {
        'host': '192.168.14.101',
        'port': 8888,
        'user': 'csg',
        'password': '123456',
        'dest_dir': '/home/csg/wave_sender_test',  # 使用绝对路径
    },
    # RK3576
    'rk3576': {
        'host': '192.168.137.204',
        'port': 22,
        'user': 'cat',
        'password': '123456',
        'dest_dir': '/home/cat/ai_inference',  # 使用绝对路径
    },
}

# 项目路径
PROJECT_DIR = r'd:\ai\prj\trae\pq_ai\pq_ai_terminal'
DEPLOY_DIR = os.path.join(PROJECT_DIR, 'deploy')

# 颜色
GREEN = '\033[0;32m'
RED = '\033[0;31m'
YELLOW = '\033[1;33m'
NC = '\033[0m'


def log_info(msg):
    print(f"{GREEN}[信息]{NC} {msg}")


def log_warn(msg):
    print(f"{YELLOW}[警告]{NC} {msg}")


def log_error(msg):
    print(f"{RED}[错误]{NC} {msg}")


def run_ssh_command(host, port, user, password, command):
    """通过paramiko执行SSH命令"""
    try:
        import paramiko
        
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        client.connect(host, port=port, username=user, password=password, timeout=10)
        
        stdin, stdout, stderr = client.exec_command(command)
        output = stdout.read().decode('utf-8')
        error = stderr.read().decode('utf-8')
        exit_status = stdout.channel.recv_exit_status()
        
        client.close()
        
        if output:
            print(output)
        if error:
            if exit_status != 0:
                log_error(error)
            else:
                print(error)
        
        return exit_status == 0, output, error
        
    except ImportError:
        log_error("需要安装paramiko: pip install paramiko")
        return False, "", ""
    except Exception as e:
        log_error(f"SSH连接失败: {e}")
        return False, "", str(e)


def upload_file(host, port, user, password, local_file, remote_file):
    """通过paramiko上传文件"""
    try:
        import paramiko
        
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        client.connect(host, port=port, username=user, password=password, timeout=10)
        
        # 确保远程目录存在
        remote_dir = os.path.dirname(remote_file)
        if remote_dir:
            stdin, stdout, stderr = client.exec_command(f"mkdir -p {remote_dir}")
            stdout.read()  # 等待命令完成
        
        sftp = client.open_sftp()
        sftp.put(local_file, remote_file)
        sftp.close()
        client.close()
        
        return True
    except Exception as e:
        log_error(f"上传失败: {e}")
        return False


def upload_dir(host, port, user, password, local_dir, remote_dir):
    """上传目录"""
    try:
        import paramiko
        
        # 先创建远程目录
        run_ssh_command(host, port, user, password, f"mkdir -p {remote_dir}")
        
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        client.connect(host, port=port, username=user, password=password, timeout=10)
        
        sftp = client.open_sftp()
        
        for root, dirs, files in os.walk(local_dir):
            # 计算相对路径
            relative_path = os.path.relpath(root, local_dir)
            remote_path = os.path.join(remote_dir, relative_path).replace('\\', '/')
            
            # 创建远程目录
            run_ssh_command(host, port, user, password, f"mkdir -p {remote_path}")
            
            # 上传文件
            for file in files:
                local_file = os.path.join(root, file)
                remote_file = os.path.join(remote_path, file).replace('\\', '/')
                sftp.put(local_file, remote_file)
                log_info(f"  上传: {local_file} -> {remote_file}")
        
        sftp.close()
        client.close()
        return True
    except Exception as e:
        log_error(f"上传目录失败: {e}")
        return False


def step1_upload_sources():
    """步骤1: 上传源文件到交叉编译服务器"""
    print(f"\n{'='*60}")
    print("  步骤1: 上传源文件到交叉编译服务器")
    print(f"{'='*60}")
    
    cc = CONFIG['cross_compile']
    
    # 上传源文件
    src_files = [
        'app/wave_sender_arm.c',
        'drivers/wave_sampler_hal.c',
        'drivers/wave_sampler_hal.h',
    ]
    
    remote_dir = cc['project_dir']
    
    for src_file in src_files:
        local_path = os.path.join(PROJECT_DIR, src_file)
        remote_path = f"{remote_dir}/{src_file}"
        
        if os.path.exists(local_path):
            log_info(f"上传 {src_file} -> {remote_path}")
            success = upload_file(cc['host'], cc['port'], cc['user'], cc['password'], 
                                local_path, remote_path)
            if not success:
                return False
        else:
            log_error(f"本地文件不存在: {local_path}")
            return False
    
    # 上传include目录
    include_local = os.path.join(PROJECT_DIR, 'include')
    include_remote = f"{remote_dir}/include"
    
    if os.path.exists(include_local):
        log_info("上传 include 目录")
        success = upload_dir(cc['host'], cc['port'], cc['user'], cc['password'],
                            include_local, include_remote)
        if not success:
            return False
    
    # 上传bin/lib目录（依赖库）
    lib_local = os.path.join(PROJECT_DIR, 'bin', 'lib')
    lib_remote = f"{remote_dir}/bin/lib"
    
    if os.path.exists(lib_local):
        log_info("上传 bin/lib 目录")
        success = upload_dir(cc['host'], cc['port'], cc['user'], cc['password'],
                            lib_local, lib_remote)
        if not success:
            return False
    
    return True


def step2_compile():
    """步骤2: 在交叉编译服务器上编译"""
    print(f"\n{'='*60}")
    print("  步骤2: 交叉编译wave_sender_arm")
    print(f"{'='*60}")
    
    cc = CONFIG['cross_compile']
    cross_compiler = cc['cross_compiler']
    project_dir = cc['project_dir']
    
    compile_cmd = f"""
cd {project_dir}

# 创建部署目录
mkdir -p deploy/lib

# 清理旧文件
rm -f deploy/wave_sender_arm

# 编译 wave_sender_arm (只链接libhd.so, 不链接libdrivers.so)
{cross_compiler}/arm-linux-gnueabihf-gcc \\
    -o deploy/wave_sender_arm \\
    app/wave_sender_arm.c \\
    drivers/wave_sampler_hal.c \\
    -Iinclude \\
    -Idrivers \\
    -Lbin/lib \\
    -lhd -lpthread -lm -lrt -ldl \\
    -static-libgcc \\
    -Wl,-rpath,'$ORIGIN/lib' \\
    -Wl,-rpath,./lib \\
    -Wl,-rpath,/custom/sys/lib/hal_lib/lib32 \\
    -Wno-unused-function \\
    -O2 2>&1

echo "编译完成"
if [ -f deploy/wave_sender_arm ]; then
    echo "✅ 编译成功"
    file deploy/wave_sender_arm
    ls -lh deploy/wave_sender_arm
else
    echo "❌ 编译失败"
fi
"""
    
    log_info("执行交叉编译...")
    success, output, error = run_ssh_command(
        cc['host'], cc['port'], cc['user'], cc['password'], compile_cmd
    )
    
    if '编译成功' in output or os.path.exists('deploy/wave_sender_arm'):
        log_info("编译成功!")
        return True
    else:
        # 查看错误信息
        if error:
            log_error(f"编译错误: {error}")
        if output:
            log_error(f"编译输出中包含错误")
        return False


def step3_download_binary():
    """步骤3: 下载编译好的二进制文件"""
    print(f"\n{'='*60}")
    print("  步骤3: 下载编译好的二进制文件")
    print(f"{'='*60}")
    
    cc = CONFIG['cross_compile']
    
    # 创建本地部署目录
    os.makedirs(DEPLOY_DIR, exist_ok=True)
    os.makedirs(os.path.join(DEPLOY_DIR, 'lib'), exist_ok=True)
    
    # 下载二进制文件
    remote_binary = f"{cc['project_dir']}/deploy/wave_sender_arm"
    local_binary = os.path.join(DEPLOY_DIR, 'wave_sender_arm')
    
    log_info(f"下载 {remote_binary} -> {local_binary}")
    
    # 使用SCP下载
    try:
        import paramiko
        
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        client.connect(cc['host'], port=cc['port'], username=cc['user'], 
                     password=cc['password'], timeout=10)
        
        sftp = client.open_sftp()
        sftp.get(remote_binary, local_binary)
        sftp.close()
        client.close()
        
        log_info("下载成功")
        
        # 检查文件大小
        file_size = os.path.getsize(local_binary)
        log_info(f"文件大小: {file_size} 字节")
        
        return True
    except Exception as e:
        log_error(f"下载失败: {e}")
        return False


def step4_create_deployment_package():
    """步骤4: 创建部署包"""
    print(f"\n{'='*60}")
    print("  步骤4: 创建部署包")
    print(f"{'='*60}")
    
    # 复制依赖库
    lib_dir = os.path.join(PROJECT_DIR, 'bin', 'lib')
    deploy_lib = os.path.join(DEPLOY_DIR, 'lib')
    
    if os.path.exists(lib_dir):
        import shutil
        for lib_file in os.listdir(lib_dir):
            src = os.path.join(lib_dir, lib_file)
            dst = os.path.join(deploy_lib, lib_file)
            if os.path.isfile(src):
                shutil.copy2(src, dst)
                log_info(f"复制库文件: {lib_file}")
    
    # 创建启动脚本
    run_script = os.path.join(DEPLOY_DIR, 'run.sh')
    with open(run_script, 'w') as f:
        f.write('''#!/bin/bash
# wave_sender_arm 启动脚本

# 设置路径
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH

# 运行程序
echo "============================================"
echo "  T536 波形采集发送程序"
echo "  功能: 采集波形 → 发送RK3576 → 接收推理"
echo "============================================"
echo ""

./wave_sender_arm "$@"
''')
    
    os.chmod(run_script, 0o755)
    log_info("创建启动脚本 run.sh")
    
    # 打包
    import tarfile
    
    package_name = 'wave_sender_arm_armhf.tar.gz'
    package_path = os.path.join(PROJECT_DIR, package_name)
    
    with tarfile.open(package_path, 'w:gz') as tar:
        for root, dirs, files in os.walk(DEPLOY_DIR):
            for file in files:
                file_path = os.path.join(root, file)
                arcname = os.path.relpath(file_path, DEPLOY_DIR)
                tar.add(file_path, arcname)
    
    log_info(f"创建部署包: {package_path}")
    log_info(f"包大小: {os.path.getsize(package_path)} 字节")
    
    return True


def step5_deploy_to_t536():
    """步骤5: 部署到T536"""
    print(f"\n{'='*60}")
    print("  步骤5: 部署到T536")
    print(f"{'='*60}")
    
    t536 = CONFIG['t536']
    package_name = 'wave_sender_arm_armhf.tar.gz'
    local_package = os.path.join(PROJECT_DIR, package_name)
    
    # 创建远程目录
    log_info(f"创建目录 {t536['dest_dir']}")
    success, _, _ = run_ssh_command(
        t536['host'], t536['port'], t536['user'], t536['password'],
        f"mkdir -p {t536['dest_dir']}"
    )
    
    if not success:
        return False
    
    # 上传部署包
    remote_package = f"{t536['dest_dir']}/{package_name}"
    log_info(f"上传部署包到T536")
    
    success = upload_file(
        t536['host'], t536['port'], t536['user'], t536['password'],
        local_package, remote_package
    )
    
    if not success:
        return False
    
    # 解压
    log_info("解压部署包")
    success, _, _ = run_ssh_command(
        t536['host'], t536['port'], t536['user'], t536['password'],
        f"cd {t536['dest_dir']} && tar xzf {package_name}"
    )
    
    if success:
        log_info("T536部署完成!")
        return True
    else:
        log_error("T536部署失败")
        return False


def step6_deploy_to_rk3576():
    """步骤6: 部署AI推理服务到RK3576"""
    print(f"\n{'='*60}")
    print("  步骤6: 部署AI推理服务到RK3576")
    print(f"{'='*60}")
    
    rk3576 = CONFIG['rk3576']
    
    # 创建远程目录
    log_info(f"创建目录 {rk3576['dest_dir']}")
    success, _, _ = run_ssh_command(
        rk3576['host'], rk3576['port'], rk3576['user'], rk3576['password'],
        f"mkdir -p {rk3576['dest_dir']}"
    )
    
    if not success:
        return False
    
    # 上传AI推理服务脚本
    server_script = os.path.join(PROJECT_DIR, 'app', 'wave_inference_server.py')
    remote_script = f"{rk3576['dest_dir']}/wave_inference_server.py"
    
    log_info("上传AI推理服务脚本")
    success = upload_file(
        rk3576['host'], rk3576['port'], rk3576['user'], rk3576['password'],
        server_script, remote_script
    )
    
    if not success:
        return False
    
    log_info("RK3576部署完成!")
    return True


def step7_start_ai_server_on_rk3576():
    """步骤7: 在RK3576上启动AI推理服务"""
    print(f"\n{'='*60}")
    print("  步骤7: 在RK3576上启动AI推理服务")
    print(f"{'='*60}")
    
    rk3576 = CONFIG['rk3576']
    usb_ip = '192.168.100.1'
    
    # 启动AI服务
    start_cmd = f"""
cd {rk3576['dest_dir']}
# 检查python3是否存在
if command -v python3 &> /dev/null; then
    echo "使用python3"
else
    echo "尝试查找python"
    python=$(which python 2>/dev/null || echo "python")
fi

# 启动服务
nohup python3 wave_inference_server.py --host {usb_ip} --port 9090 > ai_server.log 2>&1 &
echo "服务启动中..."
sleep 2
ps aux | grep wave_inference_server | grep -v grep
"""
    
    log_info("启动AI推理服务...")
    success, output, _ = run_ssh_command(
        rk3576['host'], rk3576['port'], rk3576['user'], rk3576['password'],
        start_cmd
    )
    
    if success or 'wave_inference_server' in output:
        log_info("AI推理服务启动成功!")
        log_info(f"服务日志: {rk3576['dest_dir']}/ai_server.log")
        return True
    else:
        log_warn("可能需要手动启动AI服务")
        log_info(f"SSH登录RK3576后执行:")
        log_info(f"  cd {rk3576['dest_dir']}")
        log_info(f"  python3 wave_inference_server.py --host {usb_ip} --port 9090")
        return True  # 返回True以便继续下一步


def step8_run_test_on_t536():
    """步骤8: 在T536上运行测试"""
    print(f"\n{'='*60}")
    print("  步骤8: 在T536上运行测试")
    print(f"{'='*60}")
    
    t536 = CONFIG['t536']
    rk3576_usb_ip = '192.168.100.1'  # RK3576的USB ECM地址
    
    # 运行测试
    run_cmd = f"""
cd {t536['dest_dir']}
chmod +x wave_sender_arm run.sh

# 检查USB ECM连接
echo "检查USB ECM连接..."
if ping -c 1 -W 2 {rk3576_usb_ip} &> /dev/null; then
    echo "✅ USB ECM连接正常"
else
    echo "⚠️ USB ECM连接异常，请检查网线连接"
fi

# 运行测试
echo ""
echo "开始波形采集和AI推理测试..."
./wave_sender_arm --cycles 5 --server {rk3576_usb_ip} --port 9090 --log wave_sender.log

echo ""
echo "测试完成!"
echo "查看日志: tail -f wave_sender.log"
"""
    
    log_info("在T536上运行测试...")
    log_info("注意: 测试需要实际硬件连接")
    log_info("如果T536上没有波形数据采集权限，程序可能会失败")
    log_info("")
    log_info("正在执行...")
    
    success, output, error = run_ssh_command(
        t536['host'], t536['port'], t536['user'], t536['password'],
        run_cmd
    )
    
    if output:
        print("\n--- T536 输出 ---")
        print(output)
    
    return True  # 无论成功与否都继续，让用户看到结果


def step9_view_logs():
    """步骤9: 查看日志"""
    print(f"\n{'='*60}")
    print("  步骤9: 查看日志")
    print(f"{'='*60}")
    
    t536 = CONFIG['t536']
    rk3576 = CONFIG['rk3576']
    
    # 查看T536日志
    log_info("T536端日志 (最近100行):")
    success, output, _ = run_ssh_command(
        t536['host'], t536['port'], t536['user'], t536['password'],
        f"tail -100 {t536['dest_dir']}/wave_sender.log 2>/dev/null || echo '日志文件不存在'"
    )
    
    if output:
        print(output)
    
    # 查看RK3576日志
    print(f"\n{'─'*60}")
    log_info("RK3576端AI服务日志 (最近50行):")
    success, output, _ = run_ssh_command(
        rk3576['host'], rk3576['port'], rk3576['user'], rk3576['password'],
        f"tail -50 {rk3576['dest_dir']}/ai_server.log 2>/dev/null || echo '日志文件不存在'"
    )
    
    if output:
        print(output)
    
    return True


def main():
    print("""
╔══════════════════════════════════════════════════════════════╗
║                                                              ║
║              wave_sender_arm 一键部署和测试                  ║
║                                                              ║
║  架构:                                                       ║
║  ┌─────────────┐    USB ECM     ┌─────────────┐             ║
║  │   T536      │  ──────────>  │   RK3576    │             ║
║  │  (采集端)   │  发送原始波形  │  (计算端)   │             ║
║  │             │               │             │             ║
║  │  只负责采集  │  <──────────  │  AI推理    │             ║
║  │  和发送     │  返回推理结果  │             │             ║
║  └─────────────┘               └─────────────┘             ║
║                                                              ║
╚══════════════════════════════════════════════════════════════╝
""")
    
    # 检查paramiko是否安装
    try:
        import paramiko
    except ImportError:
        print("需要安装paramiko库:")
        print("  pip install paramiko")
        sys.exit(1)
    
    # 执行部署流程
    steps = [
        ("上传源文件", step1_upload_sources),
        ("交叉编译", step2_compile),
        ("下载二进制", step3_download_binary),
        ("创建部署包", step4_create_deployment_package),
        ("部署到T536", step5_deploy_to_t536),
        ("部署到RK3576", step6_deploy_to_rk3576),
        ("启动AI服务", step7_start_ai_server_on_rk3576),
        ("运行测试", step8_run_test_on_t536),
        ("查看日志", step9_view_logs),
    ]
    
    for step_name, step_func in steps:
        try:
            result = step_func()
            if result is False:
                log_error(f"{step_name} 失败，部署中止")
                break
        except Exception as e:
            log_error(f"{step_name} 异常: {e}")
            import traceback
            traceback.print_exc()
            break
    
    print(f"\n{'='*60}")
    print("  部署流程完成!")
    print(f"{'='*60}")
    print("""
  后续操作:
  
  1. 实时查看T536日志:
     ssh -p 8888 csg@192.168.14.101
     tail -f ~/wave_sender_test/wave_sender.log
  
  2. 实时查看RK3576 AI服务日志:
     ssh cat@192.168.137.204
     tail -f ~/ai_inference/ai_server.log
  
  3. 如果AI服务未启动，手动启动:
     ssh cat@192.168.137.204
     cd ~/ai_inference
     python3 wave_inference_server.py --host 192.168.100.1 --port 9090
  
  4. 重新运行T536测试:
     ssh -p 8888 csg@192.168.14.101
     cd ~/wave_sender_test
     ./wave_sender_arm --cycles 5 --server 192.168.100.1 --port 9090
""")


if __name__ == '__main__':
    main()