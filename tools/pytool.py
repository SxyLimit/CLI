import argparse

def main():
    parser = argparse.ArgumentParser(description="输出 --foo 参数的值")
    parser.add_argument('--foo', type=str, required=True, help='输入一个字符串参数')
    args = parser.parse_args()
    print(args.foo)

if __name__ == '__main__':
    main()