# Paired probe parity (native vs Wine)

match 28 / 71 after pointer normalisation

## Matching

- audit-alpc-openif.txt
- audit-cmiocp2.txt
- audit-cmiocp2_nulloutbad.txt
- audit-cmiocp_badhwnd.txt
- audit-cmiocp_drainbad.txt
- audit-cmiocp_drainnull.txt
- audit-cmiocp_nullhwnd.txt
- audit-cmiocp_nullout.txt
- audit-cmiocp_otherhwnd.txt
- audit-combine.txt
- audit-combine_0.txt
- audit-combine_1.txt
- audit-combine_11.txt
- audit-combine_12.txt
- audit-combine_13.txt
- audit-combine_5.txt
- audit-combine_6.txt
- audit-combine_7.txt
- audit-combine_8.txt
- audit-combine_9.txt
- audit-designmode.txt
- audit-gcfp_1.txt
- audit-gcfp_4.txt
- audit-gpuprio.txt
- audit-gqsro.txt
- audit-process-events.txt
- audit-stdmex2_nullout.txt
- audit-write-overlap.txt

## Differing

### audit-acnop_0.txt (8 lines)

```diff
-export=1 session=2
+export=1 session=1
-null/ac rel=0: st=00000000 gle=dead len=240 max=242 heap=1 str=\Sessions\2\AppContainerNamedObjects\S-1-15-2-3164343934-1015664905-1319052806-1987533619-971592377-1388095234-307128736
-null/ac rel=1: st=00000000 gle=dead len=216 max=218 heap=1 str=AppContainerNamedObjects\S-1-15-2-3164343934-1015664905-1319052806-1987533619-971592377-1388095234-307128736
-proctok/ac rel=0: st=c0000030 gle=dead len=cccc max=cccc buf=PTR
+null/ac rel=0: st=00000000 gle=dead len=0 max=0 heap=1 str=
+null/ac rel=1: st=00000000 gle=dead len=0 max=0 heap=1 str=
+proctok/ac rel=0: st=00000000 gle=dead len=0 max=0 heap=1 str=
```

### audit-acnop_1.txt (2 lines)

```diff
-export=1 session=2
+export=1 session=1
```

### audit-acnop_2.txt (6 lines)

```diff
-export=1 session=2
+export=1 session=1
-sid3 n=8 rel=0 st=00000000 len=0076 max=0078 buf=set \Sessions\2\AppContainerNamedObjects\S-1-15-2-1-2-3-4-5-6-7
+sid3 n=8 rel=0 st=00000000 len=0076 max=0078 buf=set \Sessions\1\AppContainerNamedObjects\S-1-15-2-1-2-3-4-5-6-7
-sid5 n=12 rel=0 st=00000000 len=008a max=008c buf=set \Sessions\2\AppContainerNamedObjects\S-1-15-2-1-2-3-4-5-6-7\8-9-10-11
+sid5 n=12 rel=0 st=00000000 len=008a max=008c buf=set \Sessions\1\AppContainerNamedObjects\S-1-15-2-1-2-3-4-5-6-7\8-9-10-11
```

### audit-acnop_3.txt (2 lines)

```diff
-export=1 session=2
+export=1 session=1
```

### audit-alpc-send-attrs.txt (43 lines)

```diff
-SERVER_REQ case=0 valid=22000000 ctx_port_is_client_handle=0 ctx_port=PTR ctx_msg=PTR seq_nonzero=1 msgid_nonzero=1 cb=6745225 wob_tid=other wob_low=other wob_tid_is_client=0 wob_tid_is_server=0 wob_low_is_client_ctime=0 raw_tid=7e26da90 raw_low=9933cdea client_tid=00000bdc client_ctime_low=c8a17c01 server_tid=0000354c
+SERVER_REQ case=0 valid=22000000 ctx_port_is_client_handle=0 ctx_port=PTR ctx_msg=PTR seq_nonzero=0 msgid_nonzero=1 cb=0 wob_tid=other wob_low=other wob_tid_is_client=1 wob_tid_is_server=0 wob_low_is_client_ctime=1 raw_tid=0000010c raw_low=97d30c1e client_tid=0000010c client_ctime_low=97d30c1e server_tid=00000108
+SERVER reply case=0 flags=00010000 status=00000000
-CLIENT_REPLY case=0 valid=20000000 ctx_port_is_client_handle=1 ctx_port=PTR ctx_msg=PTR seq_nonzero=1 msgid_nonzero=1 cb=6745225
-SERVER reply case=0 flags=00010000 status=00000000
+CLIENT_REPLY case=0 valid=20000000 ctx_port_is_client_handle=0 ctx_port=PTR ctx_msg=PTR seq_nonzero=0 msgid_nonzero=1 cb=0
-SERVER_REQ case=1 valid=20000000 ctx_port_is_client_handle=0 ctx_port=PTR ctx_msg=PTR seq_nonzero=1 msgid_nonzero=1 cb=6745226
+SERVER_REQ case=1 valid=20000000 ctx_port_is_client_handle=0 ctx_port=PTR ctx_msg=PTR seq_nonzero=0 msgid_nonzero=1 cb=0
+SERVER reply case=1 flags=00010000 status=00000000
-CLIENT_REPLY case=1 valid=20000000 ctx_port_is_client_handle=1 ctx_port=PTR ctx_msg=PTR seq_nonzero=1 msgid_nonzero=1 cb=6745226
-SERVER reply case=1 flags=00010000 status=00000000
+CLIENT_REPLY case=1 valid=20000000 ctx_port_is_client_handle=0 ctx_port=PTR ctx_msg=PTR seq_nonzero=0 msgid_nonzero=1 cb=0
...
```

### audit-cmiocp.txt (18 lines)

```diff
-hwnd=PTR tid=4436 isgui=1
+hwnd=PTR tid=472 isgui=1
-A basic status=c0000004
+A basic access=001f0003 handles=1 pointers=2 attr=0
-A3 drain2(110) ret=0 gle=1400
+A3 drain2(74) ret=0 gle=1400
-A5 drain2(110) ret=0 gle=1400
+A5 drain2(74) ret=0 gle=1400
-A5b drain2(110) ret=0 gle=1400
+A5b drain2(74) ret=0 gle=1400
-B drain2(110) ret=0 gle=1400
+B drain2(74) ret=0 gle=1400
...
```

### audit-cmiocp2_count.txt (6 lines)

```diff
-K1 basic access=001f0003 handles=2 pointers=65537 attr=1
+K1 basic access=001f0003 handles=1 pointers=2 attr=0
-K2 basic access=001f0003 handles=2 pointers=65534 attr=1
+K2 basic access=001f0003 handles=1 pointers=2 attr=0
-K4 basic access=001f0003 handles=2 pointers=65531 attr=1
+K4 basic access=001f0003 handles=1 pointers=2 attr=0
```

### audit-cmiocp2_drainother.txt (8 lines)

```diff
-DO-noiocp drain2(300436) ret=0 gle=5
+DO-noiocp drain2(d004e) ret=0 gle=5
-DO basic access=001f0003 handles=2 pointers=65537 attr=1
+DO basic access=001f0003 handles=1 pointers=2 attr=0
-DO-other drain2(300436) ret=0 gle=87
-DO-deadthread drain2(300436) ret=0 gle=1400
+DO-other drain2(d004e) ret=0 gle=87
+DO-deadthread drain2(d004e) ret=0 gle=1400
```

### audit-cmiocp2_drainx.txt (12 lines)

```diff
-DX-unreg-pre drain2(2f0436) ret=0 gle=5
+DX-unreg-pre drain2(c004e) ret=0 gle=5
-DX basic access=001f0003 handles=2 pointers=65537 attr=1
+DX basic access=001f0003 handles=1 pointers=2 attr=0
-DX-unreg drain2(30045e) ret=0 gle=87
-DX-reg drain2(2f0436) ret=1 gle=3735928559
-DX-reg2 drain2(2f0436) ret=1 gle=3735928559
-DX-destroyed drain2(2f0436) ret=0 gle=1400
+DX-unreg drain2(30052) ret=0 gle=87
+DX-reg drain2(c004e) ret=1 gle=3735928559
+DX-reg2 drain2(c004e) ret=1 gle=3735928559
+DX-destroyed drain2(c004e) ret=0 gle=1400
```

### audit-cmiocp2_msgwait.txt (4 lines)

```diff
-MW basic access=001f0003 handles=2 pointers=65537 attr=1
+MW basic access=001f0003 handles=1 pointers=2 attr=0
-MW queuestatus=00400040 peek=0 msg=0000
+MW queuestatus=00080008 peek=1 msg=0420
```

### audit-cmiocp2_rearm.txt (6 lines)

```diff
-RA basic access=001f0003 handles=2 pointers=65537 attr=1
+RA basic access=001f0003 handles=1 pointers=2 attr=0
-RA drain2(35045e) ret=1 gle=3735928559
+RA drain2(110050) ret=1 gle=3735928559
-RA1 drain2(35045e) ret=1 gle=3735928559
+RA1 drain2(110050) ret=1 gle=3735928559
```

### audit-cmiocp2_sched.txt (12 lines)

```diff
-S basic access=001f0003 handles=2 pointers=65537 attr=1
+S basic access=001f0003 handles=1 pointers=2 attr=0
-S1 queuestatus=00400040 peek=0 msg=0000
-S1w wait status=00000102 n=0 dt=125
+S1 queuestatus=00000000 peek=0 msg=0000
+S1w wait status=00000102 n=0 dt=102
-S2 queuestatus=00400040 peek=0 msg=0000
-S2 drain2(34045e) ret=1 gle=3735928559
+S2 queuestatus=00000000 peek=0 msg=0000
+S2 drain2(70052) ret=1 gle=3735928559
-S3 queuestatus=00480048 peek=1 msg=0400
+S3 queuestatus=00080008 peek=1 msg=0400
```

### audit-cmiocp2_schedpre.txt (4 lines)

```diff
-SPnull sched(PTR) ret=0 gle=1400
-SP queuestatus=00400040 peek=0 msg=0000
+SPnull sched(PTR) ret=0 gle=3735928559
+SP queuestatus=00000000 peek=0 msg=0000
```

### audit-cmiocp_closethenre.txt (2 lines)

```diff
-C1 basic status=c0000004
+C1 basic access=001f0003 handles=1 pointers=2 attr=0
```

### audit-cmiocp_destroywin.txt (4 lines)

```diff
-W1 basic status=c0000004
+W1 basic access=001f0003 handles=1 pointers=2 attr=0
-W2 basic status=c0000004
+W2 basic access=001f0003 handles=1 pointers=2 attr=0
```

### audit-cmiocp_drainhwnd.txt (4 lines)

```diff
-DH basic status=c0000004
+DH basic access=001f0003 handles=1 pointers=2 attr=0
-DH drain2(280436) ret=1 gle=3735928559
+DH drain2(6004e) ret=1 gle=3735928559
```

### audit-cmiocp_drainpre.txt (2 lines)

```diff
-DP drain2(2280) ret=0 gle=1400
+DP drain2(214) ret=0 gle=1400
```

### audit-combine_10.txt (2 lines)

```diff
-res://x.dll/a/b + c f=0 x=0 -> 8007000e (null)
+res://x.dll/a/b + c f=0 x=0 -> 00000000 res://x.dll/a/c
```

### audit-combine_2.txt (2 lines)

```diff
-ms-appx:///Views/Main.xaml + Assets/logo.png f=0 x=0 -> 8007000e (null)
+ms-appx:///Views/Main.xaml + Assets/logo.png f=0 x=0 -> 00000000 ms-appx:///Views/Assets/logo.png
```

### audit-combine_3.txt (2 lines)

```diff
-ms-appx:///Views/Main.xaml + /Assets/logo.png f=0 x=0 -> 8007000e (null)
+ms-appx:///Views/Main.xaml + /Assets/logo.png f=0 x=0 -> 00000000 ms-appx:///Assets/logo.png
```

### audit-combine_4.txt (2 lines)

```diff
-ms-appx:///Views/Main.xaml + ms-appx-web:///x.html f=0 x=0 -> 8007000e (null)
+ms-appx:///Views/Main.xaml + ms-appx-web:///x.html f=0 x=0 -> 00000000 ms-appx-web:///x.html
```

### audit-filter-token-sd.txt (8 lines)

```diff
-TOKSD dup-default O:S-1-5-21-4126543232-1245335848-3384339037-1000G:S-1-5-21-4126543232-1245335848-3384339037-513D:(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;S-1-5-21-4126543232-1245335848-3384339037-1000)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;SY)(A;;CCDCLCSWRPRC;;;S-1-5-5-0-672044)(A;;SW;;;BA)
-TOKSD dup-custom O:S-1-5-21-4126543232-1245335848-3384339037-1000G:S-1-5-21-4126543232-1245335848-3384339037-513D:(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;SY)(A;;RC;;;OW)(A;;SW;;;BA)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;WD)
-TOKSD filtered-write-restricted O:S-1-5-21-4126543232-1245335848-3384339037-1000G:S-1-5-21-4126543232-1245335848-3384339037-513D:(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;S-1-5-21-4126543232-1245335848-3384339037-1000)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;SY)(A;;CCDCLCSWRPRC;;;S-1-5-5-0-672044)(A;;SW;;;BA)
-TOKSD filtered-plain O:S-1-5-21-4126543232-1245335848-3384339037-1000G:S-1-5-21-4126543232-1245335848-3384339037-513D:(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;S-1-5-21-4126543232-1245335848-3384339037-1000)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;SY)(A;;CCDCLCSWRPRC;;;S-1-5-5-0-672044)(A;;SW;;;BA)
+TOKSD dup-default O:S-1-5-21-0-0-0-513G:S-1-5-21-0-0-0-513D:(A;;GA;;;SY)(A;;GA;;;S-1-5-21-0-0-0-513)
+TOKSD dup-custom O:S-1-5-21-0-0-0-513G:S-1-5-21-0-0-0-513D:(A;;GA;;;SY)(A;;RC;;;OW)(A;;SW;;;BA)(A;;GA;;;WD)
+TOKSD filtered-write-restricted O:S-1-5-21-0-0-0-513G:S-1-5-21-0-0-0-513D:(A;;GA;;;SY)(A;;GA;;;S-1-5-21-0-0-0-513)
+TOKSD filtered-plain O:S-1-5-21-0-0-0-513G:S-1-5-21-0-0-0-513D:(A;;GA;;;SY)(A;;GA;;;S-1-5-21-0-0-0-513)
```

### audit-gcfp_0.txt (14 lines)

```diff
-pref ff9e5a00/ffd77800: 0=ff000000 1=ffffeb99 2=ffffc24c 3=fff89100 4=ffd47800 5=ffc06700 6=ff923e00 7=ff681a00 8=ff000000 9=ff9d5a00 a=ffb36700 20=fff08b0f d1=ffffeb99 d2=ffffffff d3=ffffffff 200=ffffffff 7fffffff=ffff00ff
-pref ff9e5a00/ffd77800: 0=ff000000 1=ffffeb99 2=ffffc24c 3=fff89100 4=ffd47800 5=ffc06700 6=ff923e00 7=ff681a00 8=ff000000 9=ff9d5a00 a=ffb36700 20=fff08b0f d1=ffffeb99 d2=ffffffff d3=ffffffff 200=ffffffff 7fffffff=ffff00ff
-pref ff000000/ff3366cc: 0=ff000000 1=ffffeb99 2=ffffc24c 3=fff89100 4=ffd47800 5=ffc06700 6=ff923e00 7=ff681a00 8=ff000000 9=ff000000 a=ff0a0a0a 20=ff6182c7 d1=ffffeb99 d2=ffffffff d3=ffffffff 200=ffffffff 7fffffff=ffff00ff
-pref ff112233/ff445566: 0=ff000000 1=ffffeb99 2=ffffc24c 3=fff89100 4=ffd47800 5=ffc06700 6=ff923e00 7=ff681a00 8=ff000000 9=ff112233 a=ff122d47 20=ff616972 d1=ffffeb99 d2=ffffffff d3=ffffffff 200=ffffffff 7fffffff=ffff00ff
-pref 00ffffff/00ffffff: 0=ff000000 1=ffffeb99 2=ffffc24c 3=fff89100 4=ffd47800 5=ffc06700 6=ff923e00 7=ff681a00 8=ff000000 9=ffffffff a=ffffffff 20=ffffffff d1=ffffeb99 d2=ffffffff d3=ffffffff 200=ffffffff 7fffffff=ffff00ff
-pref ff00ff00/ff0000ff: 0=ff000000 1=ffffeb99 2=ffffc24c 3=fff89100 4=ffd47800 5=ffc06700 6=ff923e00 7=ff681a00 8=ff000000 9=ff00ff00 a=ff15ff15 20=ff3535f2 d1=ffffeb99 d2=ffffffff d3=ffffffff 200=ffffffff 7fffffff=ffff00ff
-pref 80123456/40abcdef: 0=ff000000 1=ffffeb99 2=ffffc24c 3=fff89100 4=ffd47800 5=ffc06700 6=ff923e00 7=ff681a00 8=ff000000 9=ff123456 a=ff103f6d 20=ffd0e1f2 d1=ffffeb99 d2=ffffffff d3=ffffffff 200=ffffffff 7fffffff=ffff00ff
+pref ff9e5a00/ffd77800: 0=ff000000 1=ffffeb99 2=ffffc24c 3=fff89100 4=ffd47800 5=ffc06700 6=ff923e00 7=ff681a00 8=ffffffff 9=ffff00ff a=ffff00ff 20=ffff00ff d1=ffff00ff d2=ffffffff d3=ffff00ff 200=ffff00ff 7fffffff=ffff00ff
+pref ff9e5a00/ffd77800: 0=ff000000 1=ffffeb99 2=ffffc24c 3=fff89100 4=ffd47800 5=ffc06700 6=ff923e00 7=ff681a00 8=ffffffff 9=ffff00ff a=ffff00ff 20=ffff00ff d1=ffff00ff d2=ffffffff d3=ffff00ff 200=ffff00ff 7fffffff=ffff00ff
+pref ff000000/ff3366cc: 0=ff000000 1=ffffeb99 2=ffffc24c 3=fff89100 4=ffd47800 5=ffc06700 6=ff923e00 7=ff681a00 8=ffffffff 9=ffff00ff a=ffff00ff 20=ffff00ff d1=ffff00ff d2=ffffffff d3=ffff00ff 200=ffff00ff 7fffffff=ffff00ff
+pref ff112233/ff445566: 0=ff000000 1=ffffeb99 2=ffffc24c 3=fff89100 4=ffd47800 5=ffc06700 6=ff923e00 7=ff681a00 8=ffffffff 9=ffff00ff a=ffff00ff 20=ffff00ff d1=ffff00ff d2=ffffffff d3=ffff00ff 200=ffff00ff 7fffffff=ffff00ff
+pref 00ffffff/00ffffff: 0=ff000000 1=ffffeb99 2=ffffc24c 3=fff89100 4=ffd47800 5=ffc06700 6=ff923e00 7=ff681a00 8=ffffffff 9=ffff00ff a=ffff00ff 20=ffff00ff d1=ffff00ff d2=ffffffff d3=ffff00ff 200=ffff00ff 7fffffff=ffff00ff
...
```

### audit-gcfp_2.txt (27 lines)

```diff
-names=1227
-0 ApplicationBackground
-1 ApplicationBackgroundDarkTheme
-2 ApplicationBackgroundLightTheme
-3 ApplicationText
-4 ApplicationTextDarkTheme
-5 ApplicationTextLightTheme
-6 BootBackground
-7 BootConfirmationButton
-8 BootConfirmationButtonBackgroundDisabled
-9 BootConfirmationButtonBackgroundHover
-a BootConfirmationButtonBackgroundPressed
...
```

### audit-gcfp_3.txt (2 lines)

```diff
-pref ff9e5a00/ffd77800: 0=ff000000 1=ffffeb99 2=ffffc24c 3=fff89100 4=ffd47800 5=ffc06700 6=ff923e00 7=ff681a00 8=ff000000 9=ff9d5a00 a=ffb36700 20=fff08b0f d2=ffffffff
+pref ff9e5a00/ffd77800: 0=ff000000 1=ffffeb99 2=ffffc24c 3=fff89100 4=ffd47800 5=ffc06700 6=ff923e00 7=ff681a00 8=ffffffff 9=ffff00ff a=ffff00ff 20=ffff00ff d2=ffffffff
```

### audit-gucp_0.txt (28 lines)

```diff
-real StartColorMenu rc=0 t=4 v=ffc06700 AccentColorMenu rc=0 t=4 v=ffd47800
-real force=0 hr=00000000 start=ffc06700 accent=ffd47800 gle=0
-real force=1 hr=00000000 start=ffc06700 accent=ffd47800 gle=57005
-empty-cached force=0 hr=00000000 start=ffc06700 accent=ffd47800 gle=57005
-empty force=1 hr=00000000 start=ffc06700 accent=ffd47800 gle=57005
-empty-again force=0 hr=00000000 start=ffc06700 accent=ffd47800 gle=57005
-startonly-cached force=0 hr=00000000 start=ffc06700 accent=ffd47800 gle=57005
-startonly force=1 hr=00000000 start=ffc06700 accent=ffd47800 gle=57005
-both force=1 hr=00000000 start=ffc06700 accent=ffd47800 gle=57005
-both-changed-nocache force=0 hr=00000000 start=ffc06700 accent=ffd47800 gle=57005
-accentonly force=1 hr=00000000 start=ffc06700 accent=ffd47800 gle=57005
-start-sz force=1 hr=00000000 start=ffc06700 accent=ffd47800 gle=57005
...
```

### audit-gucp_1.txt (1 lines)

```diff
+wine: Unhandled page fault on write access to PTR at address PTR (thread 032c), starting debugger...
```

### audit-gucp_2.txt (1 lines)

```diff
+wine: Unhandled page fault on write access to PTR at address PTR (thread 0334), starting debugger...
```

### audit-gucp_3.txt (4 lines)

```diff
-override-first force=0 hr=00000000 start=ffc06700 accent=ffd47800 gle=0
-override-first force=1 hr=00000000 start=ffc06700 accent=ffd47800 gle=57005
+override-first force=0 hr=00000000 start=ff9e5a00 accent=ffd77800 gle=57005
+override-first force=1 hr=00000000 start=ff9e5a00 accent=ffd77800 gle=57005
```

### audit-gucp_4.txt (10 lines)

```diff
-pre force=0 hr=00000000 start=ffc06700 accent=ffd47800 gle=0
-changed force=0 hr=00000000 start=ffc06700 accent=ffd47800 gle=57005
-changed force=1 hr=00000000 start=ffc06700 accent=ff112233 gle=57005
-changed-later force=0 hr=00000000 start=ffc06700 accent=ff112233 gle=57005
+pre force=0 hr=00000000 start=ff9e5a00 accent=ffd77800 gle=57005
+changed force=0 hr=00000000 start=ff9e5a00 accent=ffd77800 gle=57005
+changed force=1 hr=00000000 start=ff9e5a00 accent=ffd77800 gle=57005
+changed-later force=0 hr=00000000 start=ff9e5a00 accent=ffd77800 gle=57005
-restored force=1 hr=00000000 start=ffc06700 accent=ffd77800 gle=57005
+restored force=1 hr=00000000 start=ff9e5a00 accent=ffd77800 gle=57005
```

### audit-gucp_5.txt (2 lines)

```diff
-pre force=1 hr=00000000 start=ffc06700 accent=ffd77800 gle=0
+pre force=1 hr=00000000 start=ff9e5a00 accent=ffd77800 gle=57005
```

### audit-gucp_6.txt (2 lines)

```diff
-start-missing force=1 hr=00000000 start=ff9e5a00 accent=ffd77800 gle=0
+start-missing force=1 hr=00000000 start=ff9e5a00 accent=ffd77800 gle=57005
```

### audit-gucp_7.txt (2 lines)

```diff
-pre force=1 hr=00000000 start=ff9e5a00 accent=ffd77800 gle=0
+pre force=1 hr=00000000 start=ff9e5a00 accent=ffd77800 gle=57005
```

### audit-gucp_8.txt (2 lines)

```diff
-first force=1 hr=00000000 start=ff9e5a00 accent=ffd77800 gle=0
+first force=1 hr=00000000 start=ff9e5a00 accent=ffd77800 gle=57005
```

### audit-ndr3.txt (6 lines)

```diff
-  CountRefs=0
+  CountRefs=2
-  vtbl[3] in combase.dll
-  vtbl[6] in combase.dll
+  vtbl[3] in rpcrt4.dll
+  vtbl[6] in rpcrt4.dll
```

### audit-onecore.txt (2 lines)

```diff
-en byname=1
+en byname=0
```

### audit-stdmex.txt (108 lines)

```diff
-    [outer QI {0000001b-0000-0000-c000-000000000046}]
-    [outer QI {ecc8691b-c1db-4dc0-855e-65f6c551af49}]
-    [outer QI {00000018-0000-0000-c000-000000000046}]
-    [outer QI {334d391f-0e79-3b15-c9ff-eac65dd07c42}]
-    [outer QI {00000040-0000-0000-c000-000000000046}]
-    [outer QI {334d391f-0e79-3b15-c9ff-eac65dd07c42}]
-    [outer QI {94ea2b94-e9cc-49e0-c0ff-ee64ca8f5b90}]
-    [outer QI {334d391f-0e79-3b15-c9ff-eac65dd07c42}]
-    [outer QI {77dd1250-139c-2bc3-bd95-900aced61be5}]
-    [outer QI {334d391f-0e79-3b15-c9ff-eac65dd07c42}]
-    [outer QI {bfd60505-5a1f-4e41-88ba-a6fb07202da9}]
-    [outer QI {334d391f-0e79-3b15-c9ff-eac65dd07c42}]
...
```

### audit-stdmex2.txt (6 lines)

```diff
-T6 after plain marshal(hr=00000000): hr=80010119 inner=NULL ref=2
+T6 after plain marshal(hr=00000000): hr=80010119 inner=NULL ref=3
-T9 direct MarshalInterface hr=00000000 bytes=68 ref=3
+T9 direct MarshalInterface hr=00000000 bytes=68 ref=4
-T9 ReleaseMarshalData via IMarshal hr=00000000 ref=3
+T9 ReleaseMarshalData via IMarshal hr=00000000 ref=4
```

### audit-uisettings6.txt (4 lines)

```diff
-identity same=1
+identity same=0
-MessageDuration hr=0 value=5
+MessageDuration hr=0x80004001 value=12345
```

### audit-wob-teb.txt (16 lines)

```diff
-QIT44 status=00000000 ident=7e26c078:99274447 flags=1 teb=PTR
-RTLSET_OWN_IDENT status=00000000
-SET_IDENT TEB+02b8 PTR -> PTR
-SET_IDENT diffs=1
-GET1 status=00000000 7e26c078:99274447
-QIT44_AFTER status=00000000 7e26c078:99274447 flags=0
+QIT44 status=c0000002 ident=00000001:00000000 flags=1073783000 teb=PTR
+RTLSET_OWN_IDENT status=c000000b
+SET_IDENT diffs=0
+GET1 status=00000000 00000000:00000000
+QIT44_AFTER status=c0000002 cccccccc:cccccccc flags=3435973836
-SET_ZERO TEB+02b8 PTR -> PTR
...
```

### audit-wob-ticket.txt (66 lines)

```diff
-MAIN QIT44 len=0 status=c0000004 ret=dead bytes cccccccc cccccccc cccccccc cccccccc cccccccc cccccccc
-MAIN QIT44 len=4 status=c0000004 ret=dead bytes cccccccc cccccccc cccccccc cccccccc cccccccc cccccccc
-MAIN QIT44 len=8 status=c0000004 ret=dead bytes cccccccc cccccccc cccccccc cccccccc cccccccc cccccccc
-MAIN QIT44 len=16 status=00000000 ret=10 bytes c8e5267e d39a2f99 01000000 00000000 cccccccc cccccccc
-MAIN QIT44 len=32 status=c0000004 ret=dead bytes cccccccc cccccccc cccccccc cccccccc cccccccc cccccccc
-MAIN_OTHERTHREAD QIT44 len=0 status=c0000004 ret=dead bytes cccccccc cccccccc cccccccc cccccccc cccccccc cccccccc
-MAIN_OTHERTHREAD QIT44 len=4 status=c0000004 ret=dead bytes cccccccc cccccccc cccccccc cccccccc cccccccc cccccccc
-MAIN_OTHERTHREAD QIT44 len=8 status=c0000004 ret=dead bytes cccccccc cccccccc cccccccc cccccccc cccccccc cccccccc
-MAIN_OTHERTHREAD QIT44 len=16 status=c000000d ret=dead bytes cccccccc cccccccc cccccccc cccccccc cccccccc cccccccc
-MAIN_OTHERTHREAD QIT44 len=32 status=c0000004 ret=dead bytes cccccccc cccccccc cccccccc cccccccc cccccccc cccccccc
-MAIN SET_FAB status=c000000b
+MAIN QIT44 len=0 status=c0000002 ret=dead bytes cccccccc cccccccc cccccccc cccccccc cccccccc cccccccc
...
```

### audit-wob-ticket2.txt (42 lines)

```diff
-S_INITIAL QIT44 status=00000000 10e5267e f8348198 01000000 00000000
+S_INITIAL QIT44 status=c0000002 cccccccc cccccccc cccccccc cccccccc
-S_AFTER_SET_R1 GET0 status=00000000 d0f0267e cc3e8898 cccccccc
-S_AFTER_SET_R1 GET1 status=00000000 d0f0267e cc3e8898 cccccccc
-S_AFTER_SET_R1 GET2 status=00000000 d0f0267e cc3e8898 cccccccc
-S_AFTER_SET_R1 QIT44 status=00000000 d0f0267e cc3e8898 00000000 00000000
+S_AFTER_SET_R1 GET0 status=00000000 44010000 3094589b cccccccc
+S_AFTER_SET_R1 GET1 status=00000000 44010000 3094589b cccccccc
+S_AFTER_SET_R1 GET2 status=00000000 44010000 3094589b cccccccc
+S_AFTER_SET_R1 QIT44 status=c0000002 cccccccc cccccccc cccccccc cccccccc
-S_AFTER_SET_ZERO QIT44 status=00000000 10e5267e f8348198 01000000 00000000
+S_AFTER_SET_ZERO QIT44 status=c0000002 cccccccc cccccccc cccccccc cccccccc
...
```

### audit-wob-ticket3.txt (28 lines)

```diff
-CONNFLAGS attr=00080000 status=00000000 flags=000a0000 seq=0 ctx=PTR
+CONNFLAGS attr=00080000 status=c000000d flags=cccccccc seq=3435973836 ctx=PTR
+CONNECT status=00000000
-CONNECT status=00000000
+CONNECT status=00000000
-CONNECT status=00000000
-INITIAL QIT44 status=00000000 f4f2267e b7ec1498 01000000 00000000
+INITIAL QIT44 status=c0000002 cccccccc cccccccc cccccccc cccccccc
-SIT44 received_ticket8 len=8 status=00000000 QIT44 status=00000000 c8f3267e bf471a98 00000000 00000000
+SIT44 received_ticket8 len=8 status=00000000 QIT44 status=c0000002 cccccccc cccccccc cccccccc cccccccc
-SIT44 zero8 len=8 status=00000000 QIT44 status=00000000 f4f2267e b7ec1498 01000000 00000000
+SIT44 zero8 len=8 status=00000000 QIT44 status=c0000002 cccccccc cccccccc cccccccc cccccccc
...
```

