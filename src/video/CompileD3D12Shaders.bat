dxc /Tvs_6_0 /EmainVS /Zi /Qembed_debug /Fh mainVS.csh /HV 2021 d3d12shaders.hlsl
dxc /Tps_6_0 /EmainPS /Zi /Qembed_debug /Fh mainPS.csh /HV 2021 d3d12shaders.hlsl

dxc /Tvs_6_0 /EDrawVS /Zi /Qembed_debug /Fh DrawVS.csh /HV 2021 d3d12shaders.hlsl
dxc /Tps_6_0 /EDrawPS /Zi /Qembed_debug /Fh DrawPS.csh /HV 2021 d3d12shaders.hlsl

dxc /Tcs_6_0 /EScrollXCS /Zi /Qembed_debug /Fh ScrollXCS.csh /HV 2021 d3d12shaders.hlsl
dxc /Tcs_6_0 /EScrollYCS /Zi /Qembed_debug /Fh ScrollYCS.csh /HV 2021 d3d12shaders.hlsl

dxc /Tcs_6_0 /EBlitCS /Zi /Qembed_debug /Fh BlitCS.csh /HV 2021 d3d12shaders.hlsl