WX_CONFIG = /usr/bin/wx-config

# Note: pulling in --libs all is not a good idea; need more specific targeting
WX_CFLAGS = `$(WX_CONFIG) --cflags`
WX_LIBS = `$(WX_CONFIG) --libs all`

OBJS = trial.o
CXXFLAGS = -I/usr/include/oce -I. -DLIN -DLININTEL -DHAVE_CONFIG_H -DHAVE_IOSTREAM -DHAVE_FSTREAM -DHAVE_LIMITS_H -I. -g $(WX_CFLAGS)
LDFLAGS =  -L/usr/lib/x86_64-linux-gnu -L. -lkicad_3dsg \
		-lFWOSPlugin -lPTKernel -lTKBin -lTKBinL -lTKBinXCAF -lTKBO -lTKBool -lTKBRep -lTKCAF\
		-lTKCDF -lTKFeat -lTKFillet -lTKG2d -lTKG3d -lTKGeomAlgo -lTKGeomBase -lTKHLR -lTKIGES \
		-lTKLCAF -lTKMath -lTKMesh -lTKMeshVS -lTKOffset -lTKOpenGl -lTKPCAF -lTKPLCAF -lTKPrim -lTKPShape -lTKService \
		-lTKShapeSchema -lTKShHealing -lTKStdLSchema -lTKStdSchema -lTKSTEP -lTKSTEP209 -lTKSTEPAttr -lTKSTEPBase -lTKSTL \
		-lTKTopAlgo -lTKV3d -lTKVRML -lTKXCAF -lTKXCAFSchema -lTKXDEIGES \
		-lTKXDESTEP -lTKXml -lTKXmlL -lTKXmlXCAF -lTKXSBase -lTKernel  -lGL -lGLU  $(WX_LIBS) -g

all:	$(OBJS)
			g++ -o test $(OBJS) $(LDFLAGS)

clean:
			rm -f test $(OBJS)
