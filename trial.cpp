// Dummy of process to translate IGES to SCENEGRAPH

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <map>
#include <vector>

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


std::string getShapeType( TopAbs_ShapeEnum stype )
{
    switch( stype )
    {
        case TopAbs_COMPOUND:
            return "COMPOUND";
            break;
            
        case TopAbs_COMPSOLID:
            return "COMPSOLID";
            break;
            
        case TopAbs_SOLID:
            return "SOLID";
            break;
            
        case TopAbs_SHELL:
            return "SHELL";
            break;
            
        case TopAbs_FACE:
            return "FACE";
            break;
            
        case TopAbs_WIRE:
            return "WIRE";
            break;
            
        case TopAbs_EDGE:
            return "EDGE";
            break;
            
        case TopAbs_VERTEX:
            return "VERTEX";
            break;
            
        case TopAbs_SHAPE:
            return "SHAPE";
            break;
            
        default:
            break;
    }
    
    return "UNKNOWN";
}

typedef std::map< Standard_Real, SGNODE* > COLORMAP;
typedef std::map< std::string, SGNODE* >   FACEMAP;
typedef std::map< std::string, std::vector< SGNODE* > > NODEMAP;
typedef std::pair< std::string, std::vector< SGNODE* > > NODEITEM;

struct DATA
{
    Handle( TDocStd_Document ) m_doc;
    Handle( XCAFDoc_ColorTool ) m_color;
    Handle( XCAFDoc_ShapeTool ) m_assy;
    SGNODE* scene;
    SGNODE* defaultColor;
    Quantity_Color refColor;
    NODEMAP  shapes;    // SGNODE lists representing a TopoDS_SOLID / COMPOUND
    COLORMAP colors;    // SGAPPEARANCE nodes
    FACEMAP  faces;     // SGSHAPE items representing a TopoDS_FACE
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
        // destroy any colors with no parent
        if( !colors.empty() )
        {
            COLORMAP::iterator sC = colors.begin();
            COLORMAP::iterator eC = colors.end();
            
            while( sC != eC )
            {
                if( NULL == S3D::GetSGNodeParent( sC->second ) )
                {
                    std::cout << "(destroy " << sC->second << ")\n";
                    S3D::DestroyNode( sC->second );
                }

                ++sC;
            }
            
            colors.clear();
        }

        if( defaultColor && NULL == S3D::GetSGNodeParent( defaultColor ) )
            S3D::DestroyNode( defaultColor );

        // destroy any faces with no parent
        if( !faces.empty() )
        {
            FACEMAP::iterator sF = faces.begin();
            FACEMAP::iterator eF = faces.end();
            
            while( sF != eF )
            {
                if( NULL == S3D::GetSGNodeParent( sF->second ) )
                {
                    std::cout << "(destroy " << sF->second << ")\n";
                    S3D::DestroyNode( sF->second );
                }

                ++sF;
            }
            
            faces.clear();
        }

        // destroy any shapes with no parent
        if( !shapes.empty() )
        {
            NODEMAP::iterator sS = shapes.begin();
            NODEMAP::iterator eS = shapes.end();
            
            while( sS != eS )
            {
                std::vector< SGNODE* >::iterator sV = sS->second.begin();
                std::vector< SGNODE* >::iterator eV = sS->second.end();
                
                while( sV != eV )
                {
                    if( NULL == S3D::GetSGNodeParent( *sV ) )
                        S3D::DestroyNode( *sV );
                
                    ++sV;
                }
                
                sS->second.clear();
                ++sS;
            }
            
            shapes.clear();
        }

        if( scene )
            S3D::DestroyNode( scene );
        
    }
    
    // find collection of tagged nodes
    bool GetShape( const std::string& id, std::vector< SGNODE* >*& listPtr )
    {
        listPtr = NULL;
        NODEMAP::iterator item;
        item = shapes.find( id );

        if( item == shapes.end() )
            return false;
        
        listPtr = &item->second;        
        return true;
    }

    // find collection of tagged nodes
    SGNODE* GetFace( const std::string& id )
    {
        FACEMAP::iterator item;
        item = faces.find( id );

        if( item == faces.end() )
            return NULL;
        
        return item->second;        
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


void addItems( SGNODE* parent, std::vector< SGNODE* >* lp )
{
    if( NULL == lp )
        return;
    
    std::vector< SGNODE* >::iterator sL = lp->begin();
    std::vector< SGNODE* >::iterator eL = lp->end();
    SGNODE* item;
    
    while( sL != eL )
    {
        item = *sL;
        
        if( NULL == S3D::GetSGNodeParent( item ) )
            S3D::AddSGNodeChild( parent, item );
        else
            S3D::AddSGNodeRef( parent, item );
        
        ++sL;
    }

    return;
}


bool processFace( const TopoDS_Face& face, DATA& data, Quantity_Color* color,
                  const std::string& id, SGNODE* parent, std::vector< SGNODE* >* items );


bool processShell( DATA& data, const TopoDS_Shape& shape, Quantity_Color* color, int tlvl,
                   SGNODE* parent, std::vector< SGNODE* >* items )
{
    int nFaces = 0;
    bool ret = false;    
    TopExp_Explorer tree;
    tree.Init( shape, TopAbs_FACE );

    for( ; tree.More(); tree.Next() )
    {
        const TopoDS_Face& face = TopoDS::Face( tree.Current() );

        if( processFace( face, data, color, "", parent, items ) )
            ret = true;

        ++nFaces;
    }

    tab( tlvl );
    std::cout << "* " << nFaces << " faces\n";

    return ret;
}


bool processSolid( DATA& data, const TopoDS_Shape& shape, int tlvl,
                   SGNODE* parent, std::vector< SGNODE* >* items )
{
    int nShells = 0;
    bool ret = false;
    TopExp_Explorer tree;
    tree.Init( shape, TopAbs_SHELL );

    TDF_Label label;
    data.m_assy->FindShape( shape, label );

    Quantity_Color col;
    Quantity_Color* lcolor = NULL;
    
    if( getColor( data, label, col ) )
        lcolor = &col;

    for( ; tree.More(); tree.Next() )
    {
        const TopoDS_Shape& subShape = tree.Current();

        if( processShell( data, subShape, lcolor, tlvl + 1, parent, items ) )
            ret = true;

        ++nShells;
    }

    // handle faces not associated with shells
    tree.Init( shape, TopAbs_FACE, TopAbs_SHELL );
    int nFaces = 0;

    for( ; tree.More(); tree.Next() )
    {
        const TopoDS_Face& face = TopoDS::Face( tree.Current() );

        if( processFace( face, data, lcolor, "", parent, items ) )
            ret = true;

        ++nFaces;
    }
    
    tab( tlvl );
    std::cout << "* " << nShells << " shells, " << nFaces << " faces\n";

    return ret;
}


bool processCompsolid( DATA& data, const TopoDS_Shape& shape, int tlvl,
                   SGNODE* parent, std::vector< SGNODE* >* items )
{
    int nSolids = 0;
    bool ret = false;
    TopExp_Explorer tree;
    tree.Init( shape, TopAbs_SOLID );

    for( ; tree.More(); tree.Next() )
    {
        const TopoDS_Shape& subShape = tree.Current();

        if( processSolid( data, subShape, tlvl + 1, parent, items ) )
            ret = true;

        ++nSolids;
    }
    
    tab( tlvl );
    std::cout << "* " << nSolids << " solids\n";

    return ret;
}



bool processCompound( DATA& data, const TopoDS_Shape& shape, int tlvl,
                   SGNODE* parent, std::vector< SGNODE* >* items )
{
    int nCSolids = 0;
    bool ret = false;
    TopExp_Explorer tree;
    tree.Init( shape, TopAbs_COMPSOLID );

    for( ; tree.More(); tree.Next() )
    {
        const TopoDS_Shape& subShape = tree.Current();

        if( processCompsolid( data, subShape, tlvl + 1, parent, items ) )
            ret = true;

        ++nCSolids;
    }
    
    tab( tlvl );
    std::cout << "* " << nCSolids << " compsolids\n";

    int nSolids = 0;
    tree.Init( shape, TopAbs_SOLID, TopAbs_COMPSOLID );

    for( ; tree.More(); tree.Next() )
    {
        const TopoDS_Shape& subShape = tree.Current();

        if( processSolid( data, subShape, tlvl + 1, parent, items ) )
            ret = true;

        ++nSolids;
    }
    
    tab( tlvl );
    std::cout << "* " << nSolids << " solids\n";

    int nShells = 0;
    tree.Init( shape, TopAbs_SHELL, TopAbs_SOLID );

    for( ; tree.More(); tree.Next() )
    {
        const TopoDS_Shape& subShape = tree.Current();

        // XXX - do we have a color?
        if( processShell( data, subShape, NULL, tlvl + 1, parent, items ) )
            ret = true;

        ++nShells;
    }
    
    tab( tlvl );
    std::cout << "* " << nShells << " shells\n";

    /*
    int nFaces = 0;
    tree.Init( shape, TopAbs_FACE, TopAbs_SOLID );

    for( ; tree.More(); tree.Next() )
    {
        const TopoDS_Shape& subShape = tree.Current();

        // XXX - do we have a color?
        if( processFace( TopoDS::Face( subShape ), data, NULL, "", parent, items ) )
            ret = true;

        bool processFace( const TopoDS_Face& face, DATA& data, Quantity_Color* color,
                  const std::string& id, SGNODE* parent, std::vector< SGNODE* >* items );

        ++nFaces;
    }
    
    tab( tlvl );
    std::cout << "* " << nFaces << " faces\n";
    */

    return ret;
}


bool inspect( DATA& data, const TopoDS_Shape& shape, int tlvl, SGNODE* parent,
              std::vector< SGNODE* >* items )
{
    // note: tlvl = tab level
    TDF_Label aLabel = data.m_assy->FindShape( shape, Standard_False );
    
    if( aLabel.IsNull() )
        return false;

    bool ret = false;
    std::string partID;
    getTag( aLabel, partID );
    TopAbs_ShapeEnum stype = shape.ShapeType();
    
    if( !aLabel.HasChild() )
    {
        switch( stype )
        {
            case TopAbs_COMPOUND:
                if( processCompound( data, shape, tlvl, parent, items ) )
                    ret = true;
                break;
                
            case TopAbs_COMPSOLID:
                if( processCompsolid( data, shape, tlvl, parent, items ) )
                    ret = true;
                break;
                
            case TopAbs_SOLID:
                if( processSolid( data, shape, tlvl, parent, items ) )
                    ret = true;
                break;
                
            case TopAbs_SHELL:
                // XXX - no color?
                if( processShell( data, shape, NULL, tlvl, parent, items ) )
                    ret = true;
                break;
                
            case TopAbs_FACE:
                do
                {
                    Quantity_Color col;
                    Quantity_Color* lcolor = NULL;
                    
                    if( getColor( data, aLabel, col ) )
                        lcolor = &col;

                    if( processFace( TopoDS::Face( shape ), data, lcolor, partID, parent, items ) )
                        ret = true;
                } while( 0 );
                break;

            default:
                break;
        }
        
        return ret;
    }

    TopLoc_Location loc = shape.Location();
    gp_Trsf T = loc.Transformation();
    gp_XYZ coord = T.TranslationPart();
    tab( tlvl );
    std::cout << partID << " [" << getShapeType( stype ) << "] (";
    std::cout << coord.X() << ", " << coord.Y() << ", " << coord.Z() << ")\n";

    SGNODE* pptr = parent;  // pointer to true parent
    bool hasTx = false;     // true if we need to nest a Transform
   
    if( !loc.IsIdentity() )
    {
        // Create a nested Transform
        IFSG_TRANSFORM childNode( parent );
        childNode.SetTranslation( SGPOINT( coord.X(), coord.Y(), coord.Z() ) );
        gp_XYZ axis;
        Standard_Real angle;

        if( T.GetRotation( axis, angle ) )
            childNode.SetRotation( SGVECTOR( axis.X(), axis.Y(), axis.Z() ), angle );
        
        pptr = childNode.GetRawPtr();
        hasTx = true;
    }

    std::vector< SGNODE* >* iptr = NULL;
    
    if( data.GetShape( partID, iptr ) )
    {
        tab( tlvl );
        std::cout << "* REF\n";
        addItems( pptr, iptr );

        if( NULL != items )
        {
            if( hasTx )
            {
                items->push_back( pptr );
            }
            else
            {
                if( NULL != items )
                        items->insert(items->end(), iptr->begin(), iptr->end() );
            }
        }
        
        return true;
    }
    
    tab( tlvl );
    std::cout << "* CHILD\n";

    std::vector< SGNODE* > itemList;

    TDF_ChildIterator it;
    
    for( it.Initialize( aLabel ); it.More(); it.Next() )
    {
        TopoDS_Shape subShape;
        
        if( !data.m_assy->GetShape( it.Value(), subShape ) )
            continue;

        if( inspect( data, subShape, tlvl+1, pptr, &itemList ) )
            ret = true;
    }
    
    if( ret )
    {
        data.shapes.insert( NODEITEM( partID, itemList ) );
        addItems( pptr, &itemList );
        
        if( NULL != items )
        {
            if( hasTx )
            {
                items->push_back( pptr );
            }
            else
            {
                if( NULL != items )
                        items->insert(items->end(), itemList.begin(), itemList.end() );
            }
        }
    }
    else
        std::cout << "*** FAULT\n";
        
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

    // retrieve all free shapes
    TDF_LabelSequence frshapes; 
    data.m_assy->GetFreeShapes( frshapes );
    
    int nshapes = frshapes.Length();
    bool ret = false;

    // TBD: create the top level SG node
    IFSG_TRANSFORM topNode( true );
    data.scene = topNode.GetRawPtr();
    int id = 1;
    
    while( id <= nshapes )
    {
        TopoDS_Shape shape = data.m_assy->GetShape( frshapes.Value(id) );
        
        if ( !shape.IsNull() && inspect( data, shape, 0, data.scene, NULL ) )
            ret = true;
        
        ++id;
    };

    // set to true to make extensive use of DEF/USE; otherwise
    // the output is a flat hierarchy compatible with the
    // legacy kicad VRML parser
    bool useHierarchy = true;

    // on success write out a VRML file
    if( ret )
        S3D::WriteVRML( "test.wrl", true, data.scene, useHierarchy, true );

    return 0;
}


bool processFace( const TopoDS_Face& face, DATA& data, Quantity_Color* color,
    const std::string& id, SGNODE* parent, std::vector< SGNODE* >* items )
{
    if( face.IsNull() == Standard_True)
        return false;

    SGNODE* ashape = NULL;
    
    if( !id.empty() )
        data.GetFace( id );

    if( ashape )
    {
        if( NULL == S3D::GetSGNodeParent( ashape ) )
            S3D::AddSGNodeChild( parent, ashape );
        else
            S3D::AddSGNodeRef( parent, ashape );
        
        if( NULL != items )
            items->push_back( ashape );

        return true;
    }

    TopLoc_Location loc = face.Location();
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
    
    SGNODE* ocolor = data.GetColor( color );
    
    // create a SHAPE and attach the color and data,
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

    for(int i = 1; i <= triangulation->NbNodes(); i++)
    {
        gp_XYZ v( arrPolyNodes(i).Coord() );
        tx.Transforms( v );
        vertices.push_back( SGPOINT( v.X(), v.Y(), v.Z() ) );
    }

    for(int i = 1; i <= triangulation->NbTriangles(); i++)
    {
        int a, b, c;
        arrTriangles(i).Get(a, b, c);
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

        if( data.renderBoth )
        {
            indices.push_back( c );
            indices.push_back( b );
            indices.push_back( a );
        }
    }

    vcoords.SetCoordsList( vertices.size(), &vertices[0] );
    coordIdx.SetIndices( indices.size(), &indices[0] );
    vshape.SetParent( parent );
    
    if( !id.empty() )
        data.faces.insert( std::pair< std::string,
            SGNODE* >( id, vshape.GetRawPtr() ) );
    
    return true;
}
