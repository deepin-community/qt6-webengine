// Copyright 2022 The Tint Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

////////////////////////////////////////////////////////////////////////////////
// File generated by tools/src/cmd/gen
// using the template:
//   src/tint/builtin/builtin_bench.cc.tmpl
//
// Do not modify this file directly
////////////////////////////////////////////////////////////////////////////////

#include "src/tint/builtin/builtin.h"

#include <array>

#include "benchmark/benchmark.h"

namespace tint::builtin {
namespace {

void BuiltinParser(::benchmark::State& state) {
    const char* kStrings[] = {
        "arccy",
        "3a",
        "aVray",
        "array",
        "arra1",
        "qqrJy",
        "arrll7y",
        "atppmHHc",
        "cto",
        "abGmi",
        "atomic",
        "atvmiii",
        "atWWm8c",
        "xxtomc",
        "bXgg",
        "Xu",
        "b3ol",
        "bool",
        "booE",
        "TTPol",
        "xxool",
        "4416",
        "fSVV6",
        "RR2",
        "f16",
        "96",
        "f1",
        "VOR6",
        "y3",
        "l77rrn2",
        "4032",
        "f32",
        "5",
        "u377",
        "kk2",
        "ii",
        "i3XX",
        "55399II",
        "i32",
        "irSSHHa",
        "U",
        "jV3",
        "ax2",
        "t2SGG",
        "q2x2",
        "mat2x2",
        "at2",
        "majjx",
        "a2xrf",
        "mat2xjf",
        "mNNw2x28",
        "matx2f",
        "mat2x2f",
        "mrrt2x2f",
        "Gat2x2f",
        "mat2x2FF",
        "at2h",
        "marrx2h",
        "t2x2h",
        "mat2x2h",
        "Da2xJJh",
        "ma82",
        "m11k2",
        "matx3",
        "maJx3",
        "cat2x3",
        "mat2x3",
        "mat2O3",
        "ttKavv2x__",
        "5txxx8",
        "__qatF3",
        "matqx3f",
        "33atOx3f",
        "mat2x3f",
        "mtt62x9oQQ",
        "ma2x66f",
        "mtOxzz66",
        "mat2yy3h",
        "ZaHH3Z",
        "4WWt2q3h",
        "mat2x3h",
        "mOO2x3h",
        "oatY3h",
        "matx",
        "ma2x4",
        "matw4",
        "ma2Gf",
        "mat2x4",
        "qatKKx4",
        "mmmt2x4",
        "at2x4",
        "mt2x4q",
        "mat2xbb",
        "mi2x4f",
        "mat2x4f",
        "maOO2xq",
        "matTvvx4f",
        "maFF2x4f",
        "Pa00xQh",
        "mPt2x4h",
        "ma772xss",
        "mat2x4h",
        "RRCbb2x4h",
        "mXXt2x4h",
        "qaCC2xOOh",
        "mtsuL",
        "mat3xX",
        "mat3x",
        "mat3x2",
        "qqt2",
        "mat3x22",
        "mzzyt3x",
        "matVViP",
        "mannC2f",
        "atx2AHHq",
        "mat3x2f",
        "may3x2",
        "aOOOZZf",
        "Vt12f",
        "mff__3x2h",
        "qaTMMl4h",
        "mNNt3xg",
        "mat3x2h",
        "uub3XX2h",
        "matx2h",
        "Qt882h",
        "maqx3",
        "mat3113",
        "Ft3xi22",
        "mat3x3",
        "m7t3x3",
        "NNa323",
        "VVat3x3",
        "FaWW3w11f",
        "mawwx3f",
        "Dat3x3f",
        "mat3x3f",
        "mt3x3K",
        "mat31PPhf",
        "mat33f",
        "mYYt3x3h",
        "mttHH3kk",
        "mat3rr3h",
        "mat3x3h",
        "WWas3x3h",
        "Yt3x3h",
        "mt3qfh",
        "vvafu224",
        "mt34",
        "maY34",
        "mat3x4",
        "YYa7y3E4",
        "Moatd4",
        "mt3xMM",
        "mat3x55f",
        "maN34",
        "ma3Ox33",
        "mat3x4f",
        "m3t3x4f",
        "mam3xI",
        "mnnt3r4K",
        "m3XX",
        "LatIx4h",
        "at3fh",
        "mat3x4h",
        "mYtURD4",
        "mah3x4h",
        "uuIqt3x",
        "mat4xH",
        "at4Qvv",
        "66ate",
        "mat4x2",
        "mat7x",
        "m0t55DD2",
        "IIaH4x2",
        "at4x2",
        "rat4x299",
        "mGtt41W2f",
        "mat4x2f",
        "yatx2",
        "mt4x2f",
        "IIaBB4x2f",
        "TTat4x833",
        "ddUUnntYYx2h",
        "m5CCxxdZ",
        "mat4x2h",
        "matkkq2h",
        "005itpxh",
        "maIInnx2h",
        "ccaKx",
        "mtKK",
        "ma664x3",
        "mat4x3",
        "mKKtPx",
        "maxx43",
        "matqx3",
        "MMayySrxf",
        "mat3f",
        "tx3f",
        "mat4x3f",
        "ma5F4x3f",
        "rra444z3f",
        "matWW",
        "CatZJXx3h",
        "maPPx3h",
        "mat4c3h",
        "mat4x3h",
        "matPPll6h",
        "mat993yy",
        "mat4JKKh",
        "ma_x4",
        "a4K4",
        "kVt4xz",
        "mat4x4",
        "qaSKx4",
        "mat44",
        "ma4xVV",
        "AAatIxUf",
        "mbj4f",
        "YY444x",
        "mat4x4f",
        "mao4x4",
        "mtx114f",
        "mtmxccf",
        "aJJ4x4h",
        "fCCDD4x4U",
        "mgt4x4h",
        "mat4x4h",
        "CCx4h",
        "mat4x66",
        "maN4M4h",
        "pt",
        "KW",
        "pzzee",
        "ptr",
        "",
        "w9",
        "4tnn",
        "sllDler",
        "oamp4er",
        "wEaggler",
        "sampler",
        "gamler",
        "spleS",
        "aampl",
        "sampZcRTr_comparison",
        "sampler_88TmparisOn",
        "sampler_comparim00n",
        "sampler_comparison",
        "sampler_Bmomparison",
        "Mamper_ppomarison",
        "samper_compOOrison",
        "teGtGre_1d",
        "tex11ureHH1d",
        "6exeeur_1FF",
        "texture_1d",
        "texure_1",
        "tKiilure_1d",
        "exture_1d",
        "99etvIIre_2d",
        "texture_d",
        "texture_hd",
        "texture_2d",
        "llxzzure_PPd",
        "exue2d",
        "tffqqtre_2d",
        "texJJre_2dd_arWay",
        "teXXzzre_2darray",
        "textu2_2d_array",
        "texture_2d_array",
        "tNyyture_2d_array",
        "txture_2d_rOOa",
        "textureErduaZPay",
        "22lxtredd3ee",
        "texVVe93d",
        "teture_I1d",
        "texture_3d",
        "tebture_3d",
        "ie7ure3d",
        "teotiire_3d",
        "entre_cube",
        "texturScube",
        "tex22r_cube",
        "texture_cube",
        "teC711recuGe",
        "texture8cffbe",
        "textue_cue",
        "tJJxture_SSube_array",
        "texture_9ue_arry",
        "TbbJJxture_cube_array",
        "texture_cube_array",
        "t66ture_cube_aray",
        "textur66_cubu_arra",
        "textureWubeyarray",
        "texture_deth_d",
        "texture_epth_2d",
        "texture_derth_2d",
        "texture_depth_2d",
        "tex2ure_depth_2B",
        "texture_dpBBh_2d",
        "texture_dpth_RRd",
        "tLLxture_deptVV0darray",
        "textuOOe_dethKK2d_arra",
        "textuwe_ggepth_2d_rray",
        "texture_depth_2d_array",
        "textue_depthLh2d_arpay",
        "texture_depEh2diiKrray",
        "texture_dept_2d_array",
        "textuUUe88dept_cbe",
        "texrrure_depvvh_cube",
        "texure_wepmmh_ube",
        "texture_depth_cube",
        "tjture_d44pth_cube",
        "texture_depth_cXbe",
        "t8xture_depth_cube",
        "textre_depth_cubeEEarrvvy",
        "tzzture_d9pth_cuie_array",
        "teAture_depth_QQube_GGrrJJy",
        "texture_depth_cube_array",
        "texture_depth_cusse_array",
        "texture_Pepth_cKbe_array",
        "texture_dppp_cube_attray",
        "texture_depth_multisample_2",
        "texture_depth_multisamplMMd_2d",
        "texJJure_de0th_multisampled_2d",
        "texture_depth_multisampled_2d",
        "textu8_dpth_mulisampled_2V",
        "texture_dhhpth_mKltisggmpled_2d",
        "texture_depth_multisampledf2d",
        "tex77ure_exQernal",
        "tYYxture_externa",
        "tektur_exterSal",
        "texture_external",
        "txturn_ext2rnal",
        "txture_FFternal",
        "texUPPIre_GGxuernal",
        "txtuEEe_mulaisFmpledv2d",
        "ddexBBure_mltDDeampled_2d",
        "teMture_EEulccisam55led_2",
        "texture_multisampled_2d",
        "texturemuKKtisample_d",
        "texture_multisRmpled_2d",
        "texturemulDisampl9d_2d",
        "texturestorage_1d",
        "textIre_storaa_1d",
        "texture_sto77age_1d",
        "texture_storage_1d",
        "texIure_storage_1d",
        "texture_storagedd",
        "texture_storae_1d",
        "texture_strate_d",
        "texture33stoXXcge_2d",
        "texturestorage_2E",
        "texture_storage_2d",
        "textuXXestorage_2d",
        "texture_stoBaxxe_2d",
        "texte_storWge_2G",
        "texture_storage_2d_ar66ay",
        "t0xTTr_storave_2d_array",
        "kexure_orage_2d_rray",
        "texture_storage_2d_array",
        "textppre_stoae_2d_array",
        "textre_stora11e_d_array",
        "textureystorBEgeJ2d_array",
        "textqreIImtxrage_3d",
        "texture_toFage_3d",
        "exture_Ytorage_3d",
        "texture_storage_3d",
        "heDture_sHHorage_3d",
        "texturstorage23H",
        "teture_strage_3d",
        "u2",
        "u2",
        "dd32",
        "u32",
        "uPO",
        "ba",
        "u02",
        "veh2",
        "vgY2",
        "Oec2",
        "vec2",
        "eh",
        "ppfe2",
        "vev",
        "vc2zz",
        "vaac2",
        "Ouuicf",
        "vec2f",
        "vGc2f",
        "22ecTTf",
        "dlc2f",
        "vecbh",
        "ec2BB",
        "IIScXPP",
        "vec2h",
        "jjec2h",
        "cc_c2h",
        "zz6xx2h",
        "c2",
        "4xx2N",
        "p0AAei",
        "vec2i",
        "vey2",
        "vbWW0i",
        "meMMtti",
        "du",
        "vvc_",
        "VEEc2u",
        "vec2u",
        "vec24",
        "VVeX2u",
        "veVou",
        "vec",
        "KKc3",
        "G",
        "vec3",
        "ea3",
        "OOc",
        "G",
        "v5c3f",
        "99jcfff",
        "XXvYY3R",
        "vec3f",
        "ccf",
        "v8XX5",
        "ec3",
        "ppc3cc",
        "vecvh",
        "eEE3SS",
        "vec3h",
        "vec",
        "eh",
        "ec3ww",
        "vecd99i",
        "ve99P",
        "KKec3",
        "vec3i",
        "ooMcDD",
        "vei",
        "vqi",
        "veL30",
        "vncvv66",
        "vrrn3",
        "vec3u",
        "vxxce",
        "NCCOc3u",
        "vc3u",
        "veca",
        "veNNN",
        "vec",
        "vec4",
        "vc",
        "vAYS4",
        "vec0",
        "vecaaf",
        "vmm4f",
        "ec4f",
        "vec4f",
        "vE4U",
        "veKD4",
        "v0t4__",
        "cpA",
        "ec4h",
        "vBBc4h",
        "vec4h",
        "vbnn99",
        "EEcAAh",
        "v5c66h",
        "vHc4i",
        "vecxi",
        "vzyn40",
        "vec4i",
        "ve4i",
        "kH4i",
        "veci",
        "oo4rr",
        "JJc4",
        "vcCC0",
        "vec4u",
        "xAA99F",
        "veccu",
        "vec4S",
    };
    for (auto _ : state) {
        for (auto* str : kStrings) {
            auto result = ParseBuiltin(str);
            benchmark::DoNotOptimize(result);
        }
    }
}

BENCHMARK(BuiltinParser);

}  // namespace
}  // namespace tint::builtin