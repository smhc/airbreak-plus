#!/usr/bin/env python3

import sys
from functools import reduce
from math import nan

if __name__ == "__main__":
  filenames = sys.argv[1:]
  files = [open(f, 'rb') for f in filenames]


  with open('output.bingo', 'wb') as output:
    while True:
      # Read one byte from each file
      filebytes = [f.read(1) for f in files]

      # Break the loop if we have reached the end of any file
      if not all(filebytes): break

      # Compare the bytes from all files
      byte = reduce(lambda a,b: (a==b) and a or nan, filebytes)
      if byte is nan: # The bytes are different
        output.write(b'\xFF')
      else:
        output.write(byte)
  [f.close() for f in files]