import sys, copy
sys.path.append("../..")
from py3d import *


def DrawRegistrationResult(source, target, transformation):
	source_temp = copy.deepcopy(source)
	target_temp = copy.deepcopy(target)
	source_temp.paint_uniform_color([1, 0.706, 0])
	target_temp.paint_uniform_color([0, 0.651, 0.929])
	source_temp.transform(transformation)
	DrawGeometries([source_temp, target_temp])


def DrawRegistrationResultOriginalColor(source, target, transformation):
	source_temp = copy.deepcopy(source)
	source_temp.transform(transformation)
	DrawGeometries([source_temp, target])
