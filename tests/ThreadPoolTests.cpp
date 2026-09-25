#include "Game/ThreadPool.h"
#include <array>
#include <cstdlib>
#include <iostream>

/** 交替扩缩参与线程，验证慢速非参与线程不会读取下一代任务并重复计数。 */
int main() {
	ThreadPool inlinePool(0);
	int inlineCount = 0;
	inlinePool.Dispatch(7,[&](int begin,int end) { inlineCount += end-begin; });
	if (inlineCount != 7) return EXIT_FAILURE;
	ThreadPool pool(8);
	std::array<std::atomic<int>,31> visits{};
	for (int round = 0; round < 20000; ++round) {
		const int count = round % 3 == 0 ? 1 : round % 3 == 1 ? 31 : 3;
		for (auto& n : visits) n.store(0);
		pool.Dispatch(count,[&](int begin,int end) {
			for (int i = begin; i < end; ++i) visits[i].fetch_add(1);
		});
		for (int i = 0; i < static_cast<int>(visits.size()); ++i) if (visits[i].load() != (i<count ? 1 : 0)) {
			std::cerr << "duplicate or missing item in dispatch " << round << '\n';
			return EXIT_FAILURE;
		}
	}
	std::cout << "Thread pool generation boundaries passed\n";
}
