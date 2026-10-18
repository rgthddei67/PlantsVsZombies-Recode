#pragma once

#include <array>
#include <string_view>

namespace WatersideGardenRules {
    inline constexpr int Rows = 6; // 水岸植物园行数，与泳池六行几何共用
    inline constexpr int Columns = 9; // 每行可种植列数
    inline constexpr float FertileSpeed = 4.0f; // 肥沃天然土地上的正向能力速度倍率
    // . 天然土、# 硬质石地、~ 水面；肥沃由四邻水格派生，避免维护第二份格表。
    inline constexpr std::array<std::string_view, Rows> Layout = {
        "......###", "..###.~~~", "..#~~~.##",
        "..#~~~.##", "..###.~~~", "......###"
    };

    /** 越界返回空字符；布局只描述固定地形，不保存植物占用。 */
    constexpr char Terrain(int row, int column) {
        return row >= 0 && row < Rows && column >= 0 && column < Columns
            ? Layout[row][column] : '\0';
    }

    /** 仅四邻接水格的天然土地肥沃；多侧邻水不叠加。 */
    constexpr bool IsFertile(int row, int column) {
        return Terrain(row, column) == '.'
            && (Terrain(row - 1, column) == '~' || Terrain(row + 1, column) == '~'
                || Terrain(row, column - 1) == '~' || Terrain(row, column + 1) == '~');
    }
}
