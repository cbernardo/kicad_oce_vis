// Dummy of process to translate IGES to SCENEGRAPH

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <map>

#include <TDocStd_Document.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <Quantity_Color.hxx>
#include <XCAFApp_Application.hxx>
#include <Handle_XCAFApp_Application.hxx>

#include <AIS_Shape.hxx>

#include <IGESControl_Reader.hxx>
#include <IGESCAFControl_Reader.hxx>
#include <Interface_Static.hxx>

#include <STEPControl_Reader.hxx>
#include <STEPCAFControl_Reader.hxx>

#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <Handle_XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <TDocStd_Document.hxx>

#include <BRep_Tool.hxx>
#include <BRepMesh_IncrementalMesh.hxx>

#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>

#include <Quantity_Color.hxx>
#include <Poly_Triangulation.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Precision.hxx>

#include <TDF_LabelSequence.hxx>
#include <TDF_ChildIterator.hxx>

#include "plugins/3dapi/ifsg_all.h"

// precision for mesh creation; this should be good enough for ECAD viewing
#define USER_PREC (0.07)
// angular deflection for meshing
// 10 deg (36 faces per circle) = 0.17453293
// 20 deg (18 faces per circle) = 0.34906585
#define USER_ANGLE (0.34906585)

struct DATA
{
    Handle( TDocStd_Document ) m_doc;
    Handle( XCAFDoc_ColorTool ) m_color;
    Handle( XCAFDoc_ShapeTool ) m_assy;
    SGNODE* scene;
    SGNODE* defaultColor;
    Quantity_Color refColor;
    std::map< std::string, SGNODE* > shapes;    // SGSHAPE nodes representing a TopoDS_Face
    std::map< Standard_Real, SGNODE* > colors;  // SGAPPEARANCE nodes
    bool renderBoth;
    
    DATA()
    {
        scene = NULL;
        defaultColor = NULL;
        refColor.SetValues( Quantity_NOC_BLACK );
        renderBoth = false;
    }

    ~DATA()
    {
        // destroy any shapes with no parent
        if( !shapes.empty() )
        {
            std::map< std::string, SGNODE* >::iterator sS = shapes.begin();
            std::map< std::string, SGNODE* >::iterator eS = shapes.end();
            
            while( sS != eS )
            {
                if( NULL == S3D::GetSGNodeParent( sS->second ) )
                    S3D::DestroyNode( sS->second );

                ++sS;
            }
            
            shapes.clear();
        }
        
        // destroy any colors with no parent
        if( !colors.empty() )
        {
            std::map< Standard_Real, SGNODE* >::iterator sC = colors.begin();
            std::map< Standard_Real, SGNODE* >::iterator eC = colors.end();
            
            while( sC != eC )
            {
                if( NULL == S3D::GetSGNodeParent( sC->second ) )
                    S3D::DestroyNode( sC->second );

                ++sC;
            }
            
            colors.clear();
        }
        
        if( defaultColor && NULL == S3D::GetSGNodeParent( defaultColor ) )
            S3D::DestroyNode( defaultColor );

        if( scene )
            S3D::DestroyNode( scene );
        
    }
    
    // return shape if found
    SGNODE* GetShape( const std::string& id )
    {
        std::map< std::string, SGNODE* >::iterator item;
        item = shapes.find( id );
        
        if( item != shapes.end() )
            return item->second;
        
        return NULL;
    }
    
    // return color if found; if not found, create SGAPPEARANCE
    SGNODE* GetColor( Quantity_Color* colorObj )
    {
        if( NULL == colorObj )
        {
            if( defaultColor )
                return defaultColor;
            
            IFSG_APPEARANCE app( true );
            app.SetShininess( 0.1 );
            app.SetSpecular( 0.12, 0.12, 0.12 );
            app.SetAmbient( 0.1, 0.1, 0.1 );
            app.SetDiffuse( 0.6,0.6, 0.6 );
            
            defaultColor = app.GetRawPtr();
            return defaultColor;
        }
        
        Standard_Real id = colorObj->Distance( refColor );
        std::map< Standard_Real, SGNODE* >::iterator item;
        item = colors.find( id );
        
        if( item != colors.end() )
            return item->second;
        
        IFSG_APPEARANCE app( true );
        app.SetShininess( 0.1 );
        app.SetSpecular( 0.12, 0.12, 0.12 );
        app.SetAmbient( 0.1, 0.1, 0.1 );
        app.SetDiffuse( colorObj->Red(), colorObj->Green(), colorObj->Blue() );        
        colors.insert( std::pair< Standard_Real, SGNODE* >( id, app.GetRawPtr() ) );
        
        return app.GetRawPtr();
    }
};


enum FormatType
{
    FMT_NONE = 0,
    FMT_STEP = 1,
    FMT_IGES = 2    
};


FormatType fileType( const char* aFileName )
{
    std::ifstream ifile;
    ifile.open( aFileName );
    
    if( !ifile.is_open() )
        return FMT_NONE;
    
    char iline[82];
    memset( iline, 0, 82 );
    ifile.getline( iline, 82 );
    ifile.close();
    iline[81] = 0;  // ensure NULL termination when string is too long
    
    // check for STEP in Part 21 format
    // (this can give false positives since Part 21 is not exclusively STEP)
    if( !strncmp( iline, "ISO-10303-21;", 13 ) )
        return FMT_STEP;
    
    std::string fstr = iline;
    
    // check for STEP in XML format
    // (this can give both false positive and false negatives)
    if( fstr.find( "urn:oid:1.0.10303." ) != std::string::npos )
        return FMT_STEP;

    // Note: this is a very simple test which can yield false positives; the only
    // sure method for determining if a file *not* an IGES model is to attempt
    // to load it.
    if( iline[72] == 'S' && ( iline[80] == 0 || iline[80] == 13 || iline[80] == 10 ) )
        return FMT_IGES;
    
    return FMT_NONE;
}


void tab( int lvl )
{
    if( lvl <= 0 )
        return;
    
    for( int i = 0; i < lvl; ++i )
        std::cout << "    ";
    
    return;
}


void getTag( TDF_Label& label, std::string& aTag )
{
    if( label.IsNull() )
    {
        aTag = "none";
        return;
    }
    
    std::string rtag;   // tag in reverse
    aTag.clear();
    int id = label.Tag();
    std::ostringstream ostr;
    ostr << id;
    rtag = ostr.str();
    ostr.str( "" );
    ostr.clear();
    
    TDF_Label nlab = label.Father();

    while( !nlab.IsNull() )
    {
        rtag.append( 1, ':' );
        id = nlab.Tag();
        ostr << id;
        rtag.append( ostr.str() );
        ostr.str( "" );
        ostr.clear();
        nlab = nlab.Father();
    };
    
    std::string::reverse_iterator bI = rtag.rbegin();
    std::string::reverse_iterator eI = rtag.rend();
    
    while( bI != eI )
    {
        aTag.append( 1, *bI );
        ++bI;
    }

    return;
}


bool getColor( DATA& data, TDF_Label label, Quantity_Color& color )
{
    while( true )
    {
        if( data.m_color->IsSet( label, XCAFDoc_ColorGen ) )
        {
            data.m_color->GetColor( label, XCAFDoc_ColorGen, color );
            return true;
        }
        else if( data.m_color->IsSet( label, XCAFDoc_ColorSurf ) )
        {
            data.m_color->GetColor( label, XCAFDoc_ColorSurf, color );
            return true;
        }

        label = label.Father();
        
        if( label.IsNull() )
            break;
    };
    
    return false;
}


bool processFace( const TopoDS_Face& face, DATA& data, Quantity_Color* color,
                  const std::string& id, SGNODE* parent );

bool handleSubSolid( const TopoDS_Shape& aShape, DATA& data, int tlvl, SGNODE* parent )
{
    std::string id;
    TDF_Label aLabel;
    
    data.m_assy->FindShape( aShape, aLabel );    
    getTag( aLabel, id );
    bool ret = false;
    TopExp_Explorer tree( aShape, TopAbs_FACE );
    bool hasColor;
    Quantity_Color col;
    Quantity_Color* lcolor = NULL;
    int subFace = 0;
    hasColor = getColor( data, aLabel, col );
    
    if( hasColor )
        lcolor = &col;

    for(; tree.More(); tree.Next() )
    {
        const TopoDS_Shape& shape = tree.Current();
        std::ostringstream ostr;
        ostr << id << "-" << subFace;
        std::string locID = ostr.str();

        if( shape.ShapeType() == TopAbs_FACE && processFace( TopoDS::Face( shape ),
            data, lcolor, locID, parent ) )
        {
            ++subFace;
            ret = true;
        }
    }
        
    return ret;
}


bool handleSolid( TDF_Label& aLabel, DATA& data, int tlvl, SGNODE* parent )
{
    std::string id;
    getTag( aLabel, id );

    bool ret = false;
    TopoDS_Shape aShape;
    data.m_assy->GetShape( aLabel, aShape );
    
    if( data.m_assy->IsTopLevel( aLabel ) )
    {
        TopExp_Explorer tree( aShape, TopAbs_SOLID );

        for(; tree.More(); tree.Next() )
        {
            const TopoDS_Shape& shape = tree.Current();
            TDF_Label label;
            
            if( !data.m_assy->FindShape( shape, label ) )
                continue;
            
            if( handleSubSolid( shape, data, tlvl+1, parent ) )
                ret = true;
        }
    }
    else
    {
        TopExp_Explorer tree( data.m_assy->GetShape( aLabel ), TopAbs_FACE );
        bool hasColor;
        Quantity_Color col;
        Quantity_Color* lcolor = NULL;
        int subFace = 0;
        hasColor = getColor( data, aLabel, col );
        
        if( hasColor )
            lcolor = &col;

        for(; tree.More(); tree.Next() )
        {
            const TopoDS_Shape& shape = tree.Current();
            std::ostringstream ostr;
            ostr << id << "-" << subFace;
            std::string locID = ostr.str();

            if( shape.ShapeType() == TopAbs_FACE && processFace( TopoDS::Face( shape ),
                data, lcolor, locID, parent ) )
            {
                ++subFace;
                ret = true;
            }
        }
    }
        
    return ret;
}


bool inspect( DATA& data, TopoDS_Shape& shape, int id, int tlvl, SGNODE* parent )
{
        // note: tlvl = tab level
    
        TDF_Label aLabel = data.m_assy->FindShape( shape, Standard_False );
        
        if( aLabel.IsNull() )
            return false;
        
        tab( tlvl );
        std::cout << "Shape " << id << "(";
        std::string ltag;
        getTag( aLabel, ltag );
        std::cout << ltag << ") : ";
        
        TDF_LabelSequence subs;
        Standard_Boolean hassubs = data.m_assy->GetSubShapes( aLabel, subs ); 
        
        if( hassubs )
            std::cout << "[has " << subs.Length() << " subs], ";
        else
            std::cout << "[no subs], ";
        
        Quantity_Color col;
        bool hasColor = getColor( data, aLabel, col );
        
        if( hasColor )
        {
            std::cout << "[colored " << "(" << col.Red() << ", ";
            std::cout << col.Green() << ", " << col.Blue() << ")]";
        }
        else
            std::cout << "[uncolored]";
        
        std::cout << "[type ";
        TopAbs_ShapeEnum stype = shape.ShapeType();
        
        switch( stype )
        {
            case TopAbs_COMPOUND:
                std::cout << "compound]\n";
                break;
                
            case TopAbs_COMPSOLID:
                std::cout << "compound solid]\n";
                break;
                
            case TopAbs_SOLID:
                std::cout << "solid]\n";
                break;
                
            case TopAbs_SHELL:
                std::cout << "shell]\n";
                break;
                
            case TopAbs_FACE:
                std::cout << "face]\n";
                break;
                
            case TopAbs_WIRE:
                std::cout << "wire]\n";
                break;
                
            case TopAbs_EDGE:
                std::cout << "edge]\n";
                break;
                
            case TopAbs_VERTEX:
                std::cout << "vertex]\n";
                break;
                
            case TopAbs_SHAPE:
                std::cout << "shape]\n";
                break;
                
            default:
                std::cout << "UNKNOWN]\n";
                break;
        }
        
        TopLoc_Location loc = shape.Location();
        gp_Trsf T = loc.Transformation();
        gp_XYZ coord = T.TranslationPart();
        tab( tlvl );
        std::cout << " loc( type " << shape.ShapeType() << "): ";
        std::cout << coord.X() << ", " << coord.Y() << ", " << coord.Z() << "\n";
        bool ret = false;

        if( hassubs )
        {
            IFSG_TRANSFORM childNode( parent );
            
            if( gp_Identity != T.Form() )
            {
                // data must be transformed
                gp_XYZ axis;
                Standard_Real angle;

                if( T.GetRotation( axis, angle ) )
                    childNode.SetRotation( SGVECTOR( axis.X(), axis.Y(), axis.Z() ), angle );

                childNode.SetTranslation( SGPOINT( coord.X(), coord.Y(), coord.Z() ) );
            }
            
            // process sub shapes
            int nsub = subs.Length();
            int sid = 1;
            
            while( sid <= nsub )
            {
                TopoDS_Shape sshape = data.m_assy->GetShape( subs.Value(sid) );
                
                if ( sshape.IsNull() )
                {
                    ++sid;
                    continue;
                }
                
                if( inspect( data, sshape, sid, tlvl + 1, childNode.GetRawPtr() ) )
                    ret = true;
                
                ++sid;
            };
            
            if( ret )
            {
                S3D::AddSGNodeChild( parent, childNode.GetRawPtr() );
                ret = true;
            }
            else
            {
                childNode.Destroy();
            }

        }
        else
        {
            switch( stype )
            {
                case TopAbs_FACE:
                    do
                    {
                        Quantity_Color* cp = NULL;
                        
                        if( hasColor )
                            cp = &col;
                        
                        std::cout << "(processing FACE)\n";
                        
                        if( processFace( TopoDS::Face( shape ), data, cp, ltag, parent ) )
                            ret = true;
                        
                    } while(0);
                    
                    break;
                    
                case TopAbs_COMPSOLID:
                case TopAbs_COMPOUND:
                case TopAbs_SOLID:
                    do
                    {
                        // TBD: COMPOUND: create a parent and process children
                        tab( tlvl );
                        std::cout << "(processing SOLID)\n";

                        IFSG_TRANSFORM childNode( parent );
                        
                        childNode.SetTranslation( SGPOINT( coord.X(), coord.Y(), coord.Z() ) );
                        
                        if( gp_Identity != T.Form() )
                        {
                            // data must be transformed
                            gp_XYZ axis;
                            Standard_Real angle;

                            if( T.GetRotation( axis, angle ) )
                                childNode.SetRotation( SGVECTOR( axis.X(), axis.Y(), axis.Z() ),
                                    angle );
                        }

                        if( handleSolid( aLabel, data, tlvl, childNode.GetRawPtr() ) )
                        {
                            S3D::AddSGNodeChild( parent, childNode.GetRawPtr() );
                            ret = true;
                        }
                        else
                        {
                            childNode.Destroy();
                        }

                    } while( 0 );
                    
                    break;
                    
                default:
                    break;
            }

        }
        
        return ret;
}


bool readIGES( Handle(TDocStd_Document)& m_doc, const char* fname )
{
    IGESCAFControl_Reader reader;
    IFSelect_ReturnStatus stat  = reader.ReadFile( fname );
    reader.PrintCheckLoad( Standard_False, IFSelect_ItemsByEntity ); 

    if( stat != IFSelect_RetDone )
        return false;

    // Enable user-defined shape precision
    if( !Interface_Static::SetIVal( "read.precision.mode", 1 ) )
    {
        // ERROR
        return false;
    }

    // Set the shape conversion precision to USER_PREC (default 0.0001 has too many triangles)
    if( !Interface_Static::SetRVal( "read.precision.val", USER_PREC ) )  
    {
        // ERROR
        return false;
    }

    if( !Interface_Static::SetRVal( "ShapeProcess.FixFaceSize.Tolerance", USER_PREC ) )  
    {
        // ERROR
        std::cout << "XXX: could not set face size tol\n";
    }
    
    // set other translation options
    reader.SetColorMode(true);  // use model colors
    reader.SetNameMode(false);  // don't use IGES label names
    reader.SetLayerMode(false); // ignore LAYER data

    if ( !reader.Transfer( m_doc ) )
    {
        std::cout << "* Translation failed\n";
        m_doc->Close();
        return false;
    }

    // are there any shapes to translate?
    if( reader.NbShapes() < 1 )
        return false;
    
    return true;
}


bool readSTEP( Handle(TDocStd_Document)& m_doc, const char* fname )
{
    STEPCAFControl_Reader reader;
    IFSelect_ReturnStatus stat  = reader.ReadFile( fname );
    
    if( stat != IFSelect_RetDone )
        return false;

    // Enable user-defined shape precision
    if( !Interface_Static::SetIVal( "read.precision.mode", 1 ) )
    {
        // ERROR
        return false;
    }

    // Set the shape conversion precision to USER_PREC (default 0.0001 has too many triangles)
    if( !Interface_Static::SetRVal( "read.precision.val", USER_PREC ) )  
    {
        // ERROR
        return false;
    }

    /*
    if( !Interface_Static::SetCVal( "read.step.resource.name", "" )
        || !Interface_Static::SetCVal( "read.step.sequence", "FromSTEP" ) )
    {
        std::cout << " * ERROR: cannot deactivate STEP shape healing\n";
        return false;
    }
    */

    // set other translation options
    reader.SetColorMode(true);  // use model colors
    reader.SetNameMode(false);  // don't use label names
    reader.SetLayerMode(false); // ignore LAYER data

    if ( !reader.Transfer( m_doc ) )
    {
        m_doc->Close();
        return false;
    }

    // are there any shapes to translate?
    if( reader.NbRootsForTransfer() < 1 )
        return false;
    
    return true;
}


int main( int argc, char** argv )
{
    if( argc != 2 )
        return -1;
    
    DATA data;

    Handle(XCAFApp_Application) m_app = XCAFApp_Application::GetApplication();
    m_app->NewDocument( "MDTV-XCAF", data.m_doc );
    
    switch( fileType( argv[1] ) )
    {
        case FMT_IGES:
            data.renderBoth = true;
            
            if( !readIGES( data.m_doc, argv[1] ) )
                return -1;
            break;
            
        case FMT_STEP:
            if( !readSTEP( data.m_doc, argv[1] ) )
                return -1;
            break;
            
        default:
            std::cout << "File is not an IGES or STEP file\n";
            return -1;
            break;
    }
    
    data.m_assy = XCAFDoc_DocumentTool::ShapeTool( data.m_doc->Main() );
    data.m_color = XCAFDoc_DocumentTool::ColorTool( data.m_doc->Main() );

    // retrieve all shapes at this level
    TDF_LabelSequence frshapes; 
    // Note: GetShapes appears to repeat everything, so use GetFreeShapes
    //data.m_assy->GetShapes(frshapes);
    data.m_assy->GetFreeShapes( frshapes );
    
    int nshapes = frshapes.Length();
    int id = 1;
    bool ret = false;

    // TBD: create the top level SG node
    IFSG_TRANSFORM topNode( true );
    data.scene = topNode.GetRawPtr();
    
    while( id <= nshapes )
    {
        TopoDS_Shape shape = data.m_assy->GetShape( frshapes.Value(id) );
        
        if ( shape.IsNull() )
        {
            ++id;
            continue;
        }

        if( inspect( data, shape, id, 0, data.scene ) )
            ret = true;
        
        ++id;
    };

    // on success write out a VRML file
    if( ret )
        S3D::WriteVRML( "test.wrl", true, data.scene, false, true );

    return 0;
}


bool processFace( const TopoDS_Face& face, DATA& data, Quantity_Color* color,
    const std::string& id, SGNODE* parent )
{
    if( face.IsNull() == Standard_True)
        return false;

    SGNODE* ashape = data.GetShape( id );
    
    if( ashape )
    {
        if( NULL == S3D::GetSGNodeParent( ashape ) )
            S3D::AddSGNodeChild( parent, ashape );
        else
            S3D::AddSGNodeRef( parent, ashape );

        return true;
    }

    TopLoc_Location loc;
    Standard_Boolean isTessellate (Standard_False);

    Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation( face, loc );
    
    if( triangulation.IsNull() || triangulation->Deflection() > USER_PREC + Precision::Confusion() )
        isTessellate = Standard_True;
    
    if (isTessellate) 
    {
        BRepMesh_IncrementalMesh IM(face, USER_PREC, Standard_False, USER_ANGLE );
        triangulation = BRep_Tool::Triangulation( face, loc );
    }

    if( triangulation.IsNull() == Standard_True )
        return false;
    
    Quantity_Color lcolor;
    
    // if the shape is not assigned a color, check if the face has a color
    if( NULL == color )
    {
        TDF_Label L;

        if( data.m_color->ShapeTool()->Search( face, L ) )
        {
            if( data.m_color->GetColor( L, XCAFDoc_ColorGen, lcolor )
                || data.m_color->GetColor(L, XCAFDoc_ColorCurv, lcolor )
                || data.m_color->GetColor(L, XCAFDoc_ColorSurf, lcolor ) )
                color = &lcolor;
        }
    }
        
    SGNODE* ocolor = data.GetColor( color );
    
    if( NULL == ocolor )
    {
        std::cout << "XXX - NO COLOR!\n";
        return false;
    }
    
    // TBD: create a SHAPE and attach the color and data,
    // then attach the shape to the parent and return TRUE
    IFSG_SHAPE vshape( true );
    IFSG_FACESET vface( vshape );
    IFSG_COORDS vcoords( vface );
    IFSG_COORDINDEX coordIdx( vface );

    if( NULL == S3D::GetSGNodeParent( ocolor ) )
        S3D::AddSGNodeChild( vshape.GetRawPtr(), ocolor );
    else
        S3D::AddSGNodeRef( vshape.GetRawPtr(), ocolor );

    const TColgp_Array1OfPnt&    arrPolyNodes = triangulation->Nodes();
    const Poly_Array1OfTriangle& arrTriangles = triangulation->Triangles();
    Standard_Boolean isReverse = (face.Orientation() == TopAbs_REVERSED);
    
    std::vector< SGPOINT > vertices;
    std::vector< int > indices;
    gp_Trsf tx;
    
    if( !loc.IsIdentity() )
        tx = loc.Transformation();

    for(int i = 0; i < triangulation->NbNodes(); i++)
    {
        gp_XYZ v( arrPolyNodes(i+1).Coord() );
        tx.Transforms( v );
        vertices.push_back( SGPOINT( v.X(), v.Y(), v.Z() ) );
    }

    for(int i = 0; i < triangulation->NbTriangles(); i++)
    {
        int a, b, c;
        arrTriangles(i+1).Get(a, b, c);
        a--;
        if(isReverse)
        {
            int tmp = b - 1;
            b = c - 1;
            c = tmp;
        } else {
            b--;
            c--;
        }
        
        indices.push_back( a );
        indices.push_back( b );
        indices.push_back( c );

        // XXX - it may be better to execute the equivalent of
        // renderBoth in another code module
        if( data.renderBoth )
        {
            indices.push_back( c );
            indices.push_back( b );
            indices.push_back( a );
        }
    }

    vcoords.SetCoordsList( vertices.size(), &vertices[0] );
    coordIdx.SetIndices( indices.size(), &indices[0] );
    data.shapes.insert( std::pair< std::string, SGNODE* >( id, vshape.GetRawPtr() ) );
    vshape.SetParent( parent );
    
    return true;
}
