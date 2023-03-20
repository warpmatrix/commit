# %%
import numpy
import os
import csv
import io
import matplotlib.pyplot as plt

def instDics(dics, key, val):
    if key in dics:
        dics[key] = numpy.append(dics[key], val)
    else:
        dics[key] = numpy.array([])
    return dics


# %%
recv_strs = os.popen("cat ./client_stdout | grep '\[custom\] latest_rtt' | sed 's/.*()] \[custom\] //g' |\
    awk '{print $2 $4 $6 $8}' |  sed 's/ms/000us/g' | sed 's/us//g'").read()

max_tic = -1
reader = csv.reader(io.StringIO(recv_strs))
rtts, recv_tics, smooth_rtts = {}, {}, {}
for line in reader:
    rtt, smooth_rtt, recv_tic, session_id = int(line[0]), int(line[1]), int(line[2]), int(line[3])
    rtts = instDics(rtts, session_id, rtt)
    recv_tics = instDics(recv_tics, session_id, recv_tic)
    max_tic = max(max_tic, recv_tic)

print(max_tic)
print(min(rtts[session_id]), max(rtts[session_id]), numpy.mean(rtts[session_id]))
for session_id in rtts:
    plt.plot(recv_tics[session_id], rtts[session_id])

print(min(recv_tics[session_id]), max(recv_tics[session_id]))
print(max(recv_tics[session_id]) - min(recv_tics[session_id]))
plt.show()

# %%
loss_strs = os.popen("cat ./client_stdout | grep 'losses: valid:' | \
    sed 's/.*losses: valid: [01] //g'").read().split('\n')[:-1]

# loss_nums, loss_tics = numpy.array([]), numpy.array([])
loss_nums, loss_tics, loss_sum = {}, {}, 0
for line in loss_strs:
    tic = int(line[line.find("losttic: "):].split()[1])
    loss_num = line.count("seq")
    loss_sum += loss_num
    session_id = line[line.find("session_id: "):].split()[1]
    if tic > max_tic:
        continue
    loss_nums = instDics(loss_nums, session_id, loss_num)
    loss_tics = instDics(loss_tics, session_id, tic)

print(max_tic, loss_sum)
for session_id in loss_nums:
    plt.plot(loss_tics[session_id], loss_nums[session_id])
plt.show()

# %%
inflight_strs = os.popen("cat ./client_stdout | grep 'DetectLoss()] inflight' | \
    sed 's/.*DetectLoss()] //g'").read().split('\n')[:-1]

# inflight_nums, inflight_tics = numpy.array([]), numpy.array([])
inflight_nums, inflight_tics = {}, {}
for line in inflight_strs:
    print(line)
    num = line[len("inflight: "):line.find("eventtime")].count("seq")
    tic = int(line[line.find("eventtime: "):].split()[1])
    session_id = int(line[line.find("session_id: ")].split()[1])
    if tic > max_tic:
        continue
    inflight_tics = instDics(inflight_tics, session_id, tic)
    inflight_nums = instDics(inflight_nums, session_id, num)

print(max_tic)
for session_id in inflight_nums:
    plt.plot(inflight_tics[session_id], inflight_nums[session_id])
plt.show()

# %%
send_strs = os.popen("cat ./client_stdout | grep '\[custom\] session_id' | \
    sed 's/.*\[custom\] //g' | awk '{print $(NF-4) $(NF-2) $(NF)}'").read()

reader = csv.reader(io.StringIO(send_strs))
send_tics, cwnds = {}, {}

for line in reader:
    session_id, send_tic, cwnd = int(line[0]), int(line[1]), int(line[2])
    send_tics = instDics(send_tics, session_id, send_tic)
    cwnds = instDics(cwnds, session_id, cwnd)

for sess in send_tics:
    plt.plot(send_tics[sess], cwnds[sess])

plt.show()

# %%
fig, ax1 = plt.subplots()
idx = 0
for session_id in rtts:
    ax1.plot(recv_tics[session_id], rtts[session_id], '--', color='C'+str(idx), label='rtt'+str(idx))
    idx += 1

ax2 = ax1.twinx()
for sess in loss_nums:
    ax2.plot(loss_tics[sess], loss_nums[sess], label='loss', color='C'+str(idx))
    idx += 1
for sess in send_tics:
    ax2.plot(send_tics[sess], cwnds[sess], color='C'+str(idx), label='cwnd'+str(idx-len(rtts)))
    idx += 1

fig.legend()
fig.show()

# %%
deliveryRate_strs = os.popen("cat ./client_stdout | grep 'OnDataRecv()] deliveryRate:' | sed 's/.*()] //g' |\
    awk '{print $2 $4 $6}'").read()

reader = csv.reader(io.StringIO(deliveryRate_strs))
delivery_rates, btlBws, recv_tics = {}, {}, {}
for line in reader:
    delivery_rate, btlBw, recv_tic = float(line[0]), float(line[1]), int(line[2])
    delivery_rates = instDics(delivery_rates, session_id, delivery_rate)
    btlBws = instDics(btlBws, session_id, btlBw)
    recv_tics = instDics(recv_tics, session_id, recv_tic)

print(max_tic)
idx = 0
for session_id in delivery_rates:
    plt.plot(recv_tics[session_id], delivery_rates[session_id], color='C'+str(idx), label='delivery_rate'+str(idx))
    idx += 1
for session_id in btlBws:
    plt.plot(recv_tics[session_id], btlBws[session_id], color='C'+str(idx), label='btlBw'+str(idx-len(rtts)))
    idx += 1

mean_delivery_rate = numpy.mean(delivery_rates[session_id])
print(max(btlBws[session_id]), mean_delivery_rate)
large_rate_idx = (delivery_rates[session_id] > mean_delivery_rate)
fstIdx = [i for i, x in enumerate(large_rate_idx) if x][0]
print(numpy.mean(delivery_rates[session_id][fstIdx:]))
plt.legend()
plt.show()

# %%
deliveryRate_strs = os.popen("cat ./client_stdout | grep 'OnDataRecv()] RTprop:' | sed 's/.*()] //g' |\
    awk '{print $2 $4}' | sed 's/us//g'").read()

reader = csv.reader(io.StringIO(deliveryRate_strs))
RTprops, recv_tics = {}, {}
for line in reader:
    RTprop, recv_tic = float(line[0]), float(line[1])
    RTprops = instDics(RTprops, session_id, RTprop)
    recv_tics = instDics(recv_tics, session_id, recv_tic)

print(max_tic, RTprop)
idx = 0
for session_id in RTprops:
    plt.plot(recv_tics[session_id], RTprops[session_id], color='C'+str(idx), label='RTprop'+str(idx))
    idx += 1

plt.legend()
plt.show()

# %%
fig, ax1 = plt.subplots()
idx = 0
for session_id in delivery_rates:
    ax1.plot(recv_tics[session_id], delivery_rates[session_id], color='C'+str(idx), label='delivery_rate'+str(idx))
    idx += 1

ax2 = ax1.twinx()
# ax2.plot(loss_tics, loss_nums, label='loss', color='C'+str(idx))
# idx += 1
for sess in btlBws:
    ax2.plot(recv_tics[sess], btlBws[sess], color='C'+str(idx), label='btlBw'+str(idx-len(rtts)))
    idx += 1

fig.legend()
fig.show()


