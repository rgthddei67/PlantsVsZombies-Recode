// Vulkan/OpenGL 共用。drawMode 的低两位是混合模式，其余位是解析图元类型。
// UV 描述真实轮廓；几何额外留出一个屏幕像素，避免抗锯齿边缘被承载矩形裁掉。
float boxCoverage(vec2 uv, vec2 lower, vec2 upper, vec2 footprint) {
    vec2 coverage = max(clamp((uv - lower + footprint * .5) / footprint, 0.0, 1.0)
        - clamp((uv - upper + footprint * .5) / footprint, 0.0, 1.0), vec2(0.0));
    return coverage.x * coverage.y;
}

float primitiveCoverage(vec2 uv, vec2 world, int kind) {
    if (kind == 1 || kind == 4) {
        // 平头线/实心矩形用两轴覆盖率；矩形描边用外框减内框，方角只混合一次。
        vec2 footprint = max(fwidth(uv), vec2(1e-6));
        if (kind == 1) return boxCoverage(uv, vec2(0.0), vec2(1.0), footprint);
        vec2 dx = dFdx(world), dy = dFdy(world);
        float det = dx.x * dy.y - dx.y * dy.x;
        float inverseDet = 1.0 / (abs(det) > 1e-12 ? det : 1e-12);
        vec2 gradientX = (dFdx(uv) * dy.y - dFdy(uv) * dx.y) * inverseDet;
        vec2 gradientY = (dFdy(uv) * dx.x - dFdx(uv) * dy.x) * inverseDet;
        vec2 halfWidth = .5 * sqrt(gradientX * gradientX + gradientY * gradientY);
        return clamp(boxCoverage(uv, -halfWidth, 1.0 + halfWidth, footprint)
            - boxCoverage(uv, halfWidth, 1.0 - halfWidth, footprint), 0.0, 1.0);
    }
    vec2 p = uv * 2.0 - 1.0;
    float r = length(p);
    // 对解析半径应用链式求导；直接 fwidth(length(p)) 在圆心对称的 2x2 像素上
    // 可能为零，导致亚像素小圆完全消失。
    vec2 direction = p / max(r, 1e-6);
    vec2 dr = vec2(dot(direction, dFdx(p)), dot(direction, dFdy(p)));
    float aa = max((abs(dr.x) + abs(dr.y)) * .5, 1e-6);
    if (kind == 2) return 1.0 - smoothstep(1.0 - aa, 1.0 + aa, r);

    // 把径向导数转换回世界坐标梯度，保持原 DrawCircle 的 1 逻辑像素描边。
    // 非等比缩放/镜像/斜切也保持线宽，camera/letterbox 缩放则正常放大线宽。
    vec2 dx = dFdx(world), dy = dFdy(world);
    float det = dx.x * dy.y - dx.y * dy.x;
    vec2 gradient = vec2(dr.x * dy.y - dr.y * dx.y, dx.x * dr.y - dy.x * dr.x)
        / (abs(det) > 1e-12 ? det : 1e-12);
    float halfWidth = .5 * length(gradient);
    // 外盘减内盘，细圆环在缩小时仍按覆盖率保留亮度。
    return clamp(smoothstep(1.0 - halfWidth - aa, 1.0 - halfWidth + aa, r)
        - smoothstep(1.0 + halfWidth - aa, 1.0 + halfWidth + aa, r), 0.0, 1.0);
}
