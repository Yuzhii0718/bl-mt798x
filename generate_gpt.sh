#!/bin/bash

# 设置输入和输出文件夹路径
input_folder="./atf-20231013-0ea67d76a/tools/dev/gpt_editor/mt798x"
output_folder="./output_gpt"
tools_folder="./atf-20231013-0ea67d76a/tools/dev/gpt_editor"

# 确保输出文件夹存在，不存在则创建
mkdir -p "$output_folder"

# 遍历输入文件夹中的所有json文件
for json_file in "$input_folder"/*.json; do
    # 提取文件名（不包含路径和扩展名）
    filename=$(basename -- "$json_file")
    filename_no_extension="${filename%.*}"

    # 构建输出文件路径
    output_file="$output_folder/$filename_no_extension.bin"

    # 执行Python命令
    python2.7 "$tools_folder/mtk_gpt.py" --i "$json_file" --o "$output_file"

    # 输出执行结果
    echo "转换完成: $filename"
done

echo "所有文件转换完成"