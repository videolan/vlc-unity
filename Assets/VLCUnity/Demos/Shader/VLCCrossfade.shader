Shader "Hidden/VLC/Crossfade"
{
    Properties
    {
        _MainTex ("Active Texture", 2D) = "black" {}
        _SecondaryTex ("Standby Texture", 2D) = "black" {}
        _NoiseTex ("Noise Texture", 2D) = "white" {}
        _Blend ("Blend (0 to 1)", Range(0, 1)) = 0
        _TransitionMode ("Transition Mode", Int) = 0
    }
    SubShader
    {
        Cull Off ZWrite Off ZTest Always

        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "UnityCG.cginc"

            struct appdata
            {
                float4 vertex : POSITION;
                float2 uv : TEXCOORD0;
            };

            struct v2f
            {
                float2 uv : TEXCOORD0;
                float4 vertex : SV_POSITION;
            };

            sampler2D _MainTex;
            sampler2D _SecondaryTex;
            sampler2D _NoiseTex;
            float _Blend;
            int _TransitionMode;

            v2f vert (appdata v)
            {
                v2f o;
                o.vertex = UnityObjectToClipPos(v.vertex);
                o.uv = v.uv;
                return o;
            }

            fixed4 frag (v2f i) : SV_Target
            {
                if (_TransitionMode == 1) 
                {
                    return lerp(tex2D(_MainTex, i.uv), tex2D(_SecondaryTex, i.uv), _Blend);
                }
                else if (_TransitionMode == 2) 
                {
                    float ease = _Blend * _Blend * (3.0 - 2.0 * _Blend);
                    float wipe = smoothstep(ease - 0.1, ease + 0.1, i.uv.x);
                    return lerp(tex2D(_SecondaryTex, i.uv), tex2D(_MainTex, i.uv), wipe);
                }
                else if (_TransitionMode == 3) 
                {
                    float noise = tex2D(_NoiseTex, i.uv).r;
                    float edge = smoothstep(noise - 0.1, noise + 0.1, _Blend);
                    return lerp(tex2D(_MainTex, i.uv), tex2D(_SecondaryTex, i.uv), edge);
                }
                else if (_TransitionMode == 4) 
                {
                    float ease = _Blend * _Blend * (3.0 - 2.0 * _Blend);
                    float dist = distance(i.uv, float2(0.5, 0.5));
                    float circle = smoothstep(ease * 0.8, ease * 0.8 + 0.05, dist);
                    return lerp(tex2D(_SecondaryTex, i.uv), tex2D(_MainTex, i.uv), circle);
                }
                else if (_TransitionMode == 5) 
                {
                    float env = sin(_Blend * 3.14159265);
                    
                    float disp = sin(i.uv.y * 40.0 + _Blend * 20.0) * 0.05 * env;
                    float2 distUv = i.uv + float2(disp, 0.0);

                    float tear = step(0.9, frac(sin(dot(float2(0.0, i.uv.y + _Blend), float2(12.9898, 78.233))) * 43758.5453));
                    distUv.x += tear * 0.15 * env;

                    float ca = 0.04 * env;
                    
                    fixed4 a;
                    a.r = tex2D(_MainTex, distUv + float2(ca, 0)).r;
                    a.g = tex2D(_MainTex, distUv).g;
                    a.b = tex2D(_MainTex, distUv - float2(ca, 0)).b;
                    a.a = 1.0;

                    fixed4 b;
                    b.r = tex2D(_SecondaryTex, distUv + float2(ca, 0)).r;
                    b.g = tex2D(_SecondaryTex, distUv).g;
                    b.b = tex2D(_SecondaryTex, distUv - float2(ca, 0)).b;
                    b.a = 1.0;

                    return lerp(a, b, _Blend);
                }

                return tex2D(_MainTex, i.uv);
            }
            ENDCG
        }
    }
}
