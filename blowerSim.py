index = 0
heave = 0.05
air = 0.0
pull = 0.0
jitter = 50
heaveDiff = 0.0
index2 = 0
for i in range(150):
    if (pull * 1.5) > heaveDiff:
        heaveDiff = pull * 1.5
    air += heave
    if air > 1.0:
        air = 1.0
    air += heaveDiff
    air -= pull
    if air < 0.0:
        air = 0.0
    index += 1
    index2 += 1
    if index2 == 50:
        pull = 0.1
    if index2 == 95:
        pull = 0.0
        index2 = 0
    if index == jitter:
        heaveDiff = pull * 1.5
        index = 0
    print(air)