import mmap

def mmap_reg():
    with open(filename, mode="r", encoding="utf8") as file_obj:
        with mmap.mmap(file_obj.fileno(), length=0, access=mmap.ACCESS_READ) as mmap_obj:
            text = mmap_obj.read()
            print(text)

def main():
  mmap_reg()


if __name__ == “__main__“:
    main()
