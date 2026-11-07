// 输入为采样后还原的 [0,1] 直通 RGB；只替换 HSL 运算，不改变采样、量化或预乘顺序。
vec3 applyLumSat(vec3 rgb, float lumScale, float satScale) {
    float oldL = (max(max(rgb.r, rgb.g), rgb.b) + min(min(rgb.r, rgb.g), rgb.b)) * 0.5;
    float newL = oldL * lumScale;
    float denominator = min(oldL, 1.0 - oldL);
    // 保留 newL > 1 时的有符号色度，最终与旧算法一样逐通道 clamp。
    // 保持色相时，RGB 相对亮度的偏差只需乘以新旧色度之比，无需提取色相。
    float ratio = denominator > 0.0 ? satScale * min(newL, 1.0 - newL) / denominator : 0.0;
    return clamp((rgb - vec3(oldL)) * ratio + vec3(newL), 0.0, 1.0);
}
