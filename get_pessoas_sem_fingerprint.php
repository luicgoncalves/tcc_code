<?php
// Configurações do banco de dados
$host = "localhost";  // Ajuste para o IP/host do seu servidor MySQL
$user = "user";
$password = "senha";
$database = "banco";

// Conexão com o banco de dados
$conn = mysqli_connect($host, $user, $password, $database);

// Verifica a conexão
if (!$conn) {
    die(json_encode(["erro" => "Falha na conexão: " . mysqli_connect_error()]));
}

// Obtém o código do dispositivo da URL (ex: api_acesso.php?codigo=D123)
$codigo_dispositivo = isset($_GET['codigo']) ? mysqli_real_escape_string($conn, $_GET['codigo']) : '';

// Se o código não for informado, retorna erro
if (empty($codigo_dispositivo)) {
    echo json_encode(["erro" => "Código do dispositivo não informado"]);
    exit;
}

// Query para buscar os registros filtrados
$sql = "SELECT pessoa_idcard, pessoa_nome, pessoa_id, dispositivo_codigo
        FROM vw_acesso_pendentes
        WHERE dispositivo_codigo = '$codigo_dispositivo'";

$result = mysqli_query($conn, $sql);

// Monta o JSON de resposta
$dados = [];
while ($row = mysqli_fetch_assoc($result)) {
    $dados[] = $row;
}

// Retorna os dados como JSON
header('Content-Type: application/json');
echo json_encode($dados, JSON_PRETTY_PRINT);

// Fecha a conexão
mysqli_close($conn);
?>
