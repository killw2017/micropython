x = list(range(10))
print(x[::-1])
print(x[::2])
print(x[::-2])

x = list(range(9))
print(x[::-1])
print(x[::2])
print(x[::-2])

x = list(range(5))
print(x[:0:-1])
print(x[:1:-1])
print(x[:2:-1])
print(x[0::-1])
print(x[1::-1])
print(x[2::-1])

x = list(range(5))
print(x[0:0:-1])
print(x[4:4:-1])
print(x[5:5:-1])

x = list(range(10))
print(x[-1:-1:-1])
print(x[-1:-2:-1])
print(x[-1:-11:-1])
print(x[-10:-11:-1])

print(x[:-15:-1])

if len([][::-1]):
	print('Skipping crashing tests with negative step.')
else:
	print([][::-1])
	print([1][::-1])
	print([][:-20:-1])
	print([1][:-20:-1])
	print([][-20::-1])
	print([1][-20::-1])
