"""Fast Marching Method (FMM) and Fast Marching Square (FM²) Route Planner.

Implements FM² path planning and corridor recovery. Focuses on preventing
"shore-hugging" by smoothing obstacle potentials with Euclidean distance fields.
"""

from __future__ import annotations

import heapq
import math


class Fm2Planner:
    """Computes collision-free, smooth trajectory pathing using the FM² potential field."""

    def __init__(self, width: int = 100, height: int = 100, cell_size_m: float = 10.0) -> None:
        self._width = width
        self._height = height
        self._cell_size = cell_size_m
        self._grid = [[1.0 for _ in range(height)] for _ in range(width)]

    def set_obstacles(self, obstacles: list[tuple[int, int]]) -> None:
        """Inject grid coordinate cells that represent absolute hazards (cost = 255)."""
        for x, y in obstacles:
            if 0 <= x < self._width and 0 <= y < self._height:
                self._grid[x][y] = 0.0

    def compute_edt(self) -> list[list[float]]:
        """Calculate Euclidean Distance Transform (EDT) to obstacles."""
        edt = [[999.0 for _ in range(self._height)] for _ in range(self._width)]

        # 1. Initialize obstacle cells with distance 0
        queue = []
        for x in range(self._width):
            for y in range(self._height):
                if self._grid[x][y] == 0.0:
                    edt[x][y] = 0.0
                    heapq.heappush(queue, (0.0, x, y))

        # 2. Dijkstra-style flood fill to compute EDT
        while queue:
            dist, x, y = heapq.heappop(queue)
            if dist > edt[x][y]:
                continue

            for dx, dy in [(-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (-1, 1), (1, -1), (1, 1)]:
                nx, ny = x + dx, y + dy
                if 0 <= nx < self._width and 0 <= ny < self._height:
                    step_dist = math.sqrt(dx*dx + dy*dy) * self._cell_size
                    new_dist = dist + step_dist
                    if new_dist < edt[nx][ny]:
                        edt[nx][ny] = new_dist
                        heapq.heappush(queue, (new_dist, nx, ny))
        return edt

    def build_speed_map(self, k: float = 0.02) -> list[list[float]]:
        """Map EDT distances to speed coefficients via Sigmoid to prevent shore-hugging."""
        edt = self.compute_edt()
        speed_map = [[0.0 for _ in range(self._height)] for _ in range(self._width)]

        for x in range(self._width):
            for y in range(self._height):
                # Sigmoid scaling: near obstacles, speed drops sharply; far away it approaches 1.0
                dist = edt[x][y]
                if dist == 0.0:
                    speed_map[x][y] = 0.0001  # Safeguard division by zero
                else:
                    speed_map[x][y] = 1.0 - math.exp(-k * (dist ** 2))

        return speed_map

    def compute_fmm_arrival_times(
        self, start: tuple[int, int], speed_map: list[list[float]]
    ) -> list[list[float]]:
        """Compute the arrival time grid from a start node using FMM."""
        times = [[99999.0 for _ in range(self._height)] for _ in range(self._width)]
        times[start[0]][start[1]] = 0.0

        # Priority queue for the wave front expansion (FMM Dijkstra-like propagation)
        pq = [(0.0, start[0], start[1])]

        while pq:
            t, x, y = heapq.heappop(pq)
            if t > times[x][y]:
                continue

            for dx, dy in [(-1, 0), (1, 0), (0, -1), (0, 1)]:
                nx, ny = x + dx, y + dy
                if 0 <= nx < self._width and 0 <= ny < self._height:
                    v = speed_map[nx][ny]
                    step_time = self._cell_size / v
                    new_time = t + step_time
                    if new_time < times[nx][ny]:
                        times[nx][ny] = new_time
                        heapq.heappush(pq, (new_time, nx, ny))
        return times

    def plan_path(
        self, start: tuple[int, int], goal: tuple[int, int]
    ) -> list[tuple[float, float]]:
        """Plan a path from start to goal utilizing FM² gradient descent.

        Returns:
            list: coordinate points of the safe, collision-free, smooth trajectory.
        """
        # 1. Build smooth speed map and compute FMM arrival time map from goal
        speed_map = self.build_speed_map()
        times = self.compute_fmm_arrival_times(goal, speed_map)

        # 2. Gradient descent back from start to goal
        path = [(float(start[0]), float(start[1]))]
        curr = (float(start[0]), float(start[1]))

        # Safety iteration limit to prevent infinite search loops
        max_iters = 1000
        for _ in range(max_iters):
            cx, cy = int(round(curr[0])), int(round(curr[1]))
            if (cx, cy) == goal or times[cx][cy] > 9999.0:
                break

            # Compute negative gradient of time map
            grad_x, grad_y = 0.0, 0.0
            min_neighbor_time = times[cx][cy]
            next_step = None

            for dx, dy in [(-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (-1, 1), (1, -1), (1, 1)]:
                nx, ny = cx + dx, cy + dy
                if 0 <= nx < self._width and 0 <= ny < self._height:
                    val = times[nx][ny]
                    if val < min_neighbor_time:
                        min_neighbor_time = val
                        # Normalize vector steps
                        step_len = math.sqrt(dx*dx + dy*dy)
                        grad_x = -dx / step_len
                        grad_y = -dy / step_len
                        next_step = (float(cx + dx), float(cy + dy))

            if next_step is None:
                # Trapped in local minima
                break

            # Perform continuous step along the gradient
            step_size = 0.5
            curr = (curr[0] + grad_x * step_size, curr[1] + grad_y * step_size)
            path.append(curr)

            # Close enough to goal check
            if math.hypot(curr[0] - goal[0], curr[1] - goal[1]) < 1.0:
                path.append((float(goal[0]), float(goal[1])))
                break

        return path
